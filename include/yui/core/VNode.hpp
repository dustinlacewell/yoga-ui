#pragma once

#include "Props.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace yui {

enum class PrimitiveType { Box, Text, Input, Scroll, Canvas };

// PrimitiveType must mirror the alternative order of PropsVariant so that
// VNode::type() can derive the primitive from props.index() (single source of
// truth). If PropsVariant is reordered, update PrimitiveType to match.
template<PrimitiveType T, typename Props>
inline constexpr bool primitive_matches_v =
    std::is_same_v<std::variant_alternative_t<static_cast<size_t>(T), PropsVariant>, Props>;

static_assert(primitive_matches_v<PrimitiveType::Box, BoxProps>);
static_assert(primitive_matches_v<PrimitiveType::Text, TextProps>);
static_assert(primitive_matches_v<PrimitiveType::Input, InputProps>);
static_assert(primitive_matches_v<PrimitiveType::Scroll, ScrollProps>);
static_assert(primitive_matches_v<PrimitiveType::Canvas, CanvasProps>);

// Forward declarations
struct VNode;
class ComponentContext;

// ComponentFn type - component render function signature
using ComponentFn = std::function<VNode(ComponentContext&)>;

// Component - holds a deferred render function
// Must be fully defined BEFORE Child variant (std::variant requires complete types)
struct Component {
    static constexpr int64_t NO_INT_KEY = INT64_MIN;

    ComponentFn fn;
    std::string key;
    int64_t intKey = NO_INT_KEY;

    // Unconditional (release too): feeds the always-on rules-of-hooks / any_cast
    // hook diagnostics, which must name the offending component in any build. A
    // bare const char* to a literal — no storage cost when unset.
    const char* debugName = nullptr;

    // Accept any const-callable that returns VNode from ComponentContext&
    // Rejects mutable lambdas — component functions must be repeatable (called every re-render)
    template<typename F,
             typename = std::enable_if_t<std::is_invocable_r_v<VNode, const F&, ComponentContext&>>>
    Component(F f) : fn(std::move(f)) {}

    // Key for reconciliation (string)
    Component& setKey(std::string k) {
        key = std::move(k);
        return *this;
    }

    // Key for reconciliation (integer - more efficient)
    Component& setKey(int64_t k) {
        intKey = k;
        return *this;
    }

    // Set debug name (for better hook-diagnostic messages, in any build)
    Component& setName(const char* name) {
        debugName = name;
        return *this;
    }

    // Key helpers
    bool hasKey() const { return intKey != NO_INT_KEY || !key.empty(); }
    bool hasIntKey() const { return intKey != NO_INT_KEY; }
    bool hasStringKey() const { return !key.empty(); }
};

// Child can be either a VNode (immediate) or a Component (deferred)
using Child = std::variant<VNode, Component>;

// VNode is a plain data record. Its primitive type is derived from props (the
// single source of truth), never stored independently. The fluent setter API
// lives on the per-primitive builder types below; VNode itself is constructed
// only by the factory functions and the builders' conversion operators.
struct VNode {
    static constexpr int64_t NO_INT_KEY = INT64_MIN;

    std::string key;
    int64_t intKey = NO_INT_KEY;
    PropsVariant props;
    std::vector<Child> children;
    bool isEmpty = false;

    // Primitive type derived from the active props alternative.
    PrimitiveType type() const { return static_cast<PrimitiveType>(props.index()); }

    // Empty node (skipped by reconciler). Defaults to a Box (props.index() == 0).
    static VNode empty() {
        VNode n;
        n.isEmpty = true;
        return n;
    }

    // Key helpers
    bool hasKey() const { return intKey != NO_INT_KEY || !key.empty(); }
    bool hasIntKey() const { return intKey != NO_INT_KEY; }
    bool hasStringKey() const { return !key.empty(); }
};

// --- Builder API ---
//
// Each primitive has a dedicated builder that owns a VNode and exposes only the
// setters valid for that primitive. Universal setters (layout, events, key,
// children) live on the CRTP BuilderBase and return Derived& so chaining keeps
// the concrete builder type. Primitive-specific setters access their props
// alternative directly and safely — the builder knows its type, so a
// wrong-primitive setter call (e.g. Text(...).value(...)) is a compile error.

template<class Derived>
class BuilderBase {
public:
    // --- Keys ---
    Derived& setKey(std::string k) {
        node_.key = std::move(k);
        return self();
    }
    Derived& setKey(int64_t k) {
        node_.intKey = k;
        return self();
    }

    // --- Children ---
    Derived& setChildren(std::vector<Child> c) {
        node_.children = std::move(c);
        return self();
    }

    // --- Layout props (work on any primitive) ---
    Derived& width(float v) { return layout([&](LayoutProps& p) { p.width = v; }); }
    Derived& height(float v) { return layout([&](LayoutProps& p) { p.height = v; }); }
    Derived& widthPercent(float v) { return layout([&](LayoutProps& p) { p.widthPercent = v; }); }
    Derived& heightPercent(float v) { return layout([&](LayoutProps& p) { p.heightPercent = v; }); }
    Derived& minWidth(float v) { return layout([&](LayoutProps& p) { p.minWidth = v; }); }
    Derived& minHeight(float v) { return layout([&](LayoutProps& p) { p.minHeight = v; }); }
    Derived& maxWidth(float v) { return layout([&](LayoutProps& p) { p.maxWidth = v; }); }
    Derived& maxHeight(float v) { return layout([&](LayoutProps& p) { p.maxHeight = v; }); }

    Derived& flexGrow(float v) { return layout([&](LayoutProps& p) { p.flexGrow = v; }); }
    Derived& flexShrink(float v) { return layout([&](LayoutProps& p) { p.flexShrink = v; }); }
    Derived& flexBasis(float v) { return layout([&](LayoutProps& p) { p.flexBasis = v; }); }
    Derived& flexDirection(FlexDirection v) {
        return layout([&](LayoutProps& p) { p.flexDirection = v; });
    }
    Derived& flexWrap(FlexWrap v) { return layout([&](LayoutProps& p) { p.flexWrap = v; }); }
    Derived& justifyContent(JustifyContent v) {
        return layout([&](LayoutProps& p) { p.justifyContent = v; });
    }
    Derived& alignItems(AlignItems v) { return layout([&](LayoutProps& p) { p.alignItems = v; }); }
    Derived& alignContent(AlignContent v) {
        return layout([&](LayoutProps& p) { p.alignContent = v; });
    }
    Derived& alignSelf(AlignSelf v) { return layout([&](LayoutProps& p) { p.alignSelf = v; }); }
    Derived& positionType(PositionType v) {
        return layout([&](LayoutProps& p) { p.positionType = v; });
    }
    Derived& display(Display v) { return layout([&](LayoutProps& p) { p.display = v; }); }

    Derived& padding(float v) { return layout([&](LayoutProps& p) { p.padding = v; }); }
    Derived& paddingTop(float v) { return layout([&](LayoutProps& p) { p.paddingTop = v; }); }
    Derived& paddingRight(float v) { return layout([&](LayoutProps& p) { p.paddingRight = v; }); }
    Derived& paddingBottom(float v) { return layout([&](LayoutProps& p) { p.paddingBottom = v; }); }
    Derived& paddingLeft(float v) { return layout([&](LayoutProps& p) { p.paddingLeft = v; }); }

    Derived& margin(float v) { return layout([&](LayoutProps& p) { p.margin = v; }); }
    Derived& marginTop(float v) { return layout([&](LayoutProps& p) { p.marginTop = v; }); }
    Derived& marginRight(float v) { return layout([&](LayoutProps& p) { p.marginRight = v; }); }
    Derived& marginBottom(float v) { return layout([&](LayoutProps& p) { p.marginBottom = v; }); }
    Derived& marginLeft(float v) { return layout([&](LayoutProps& p) { p.marginLeft = v; }); }

    Derived& gap(float v) { return layout([&](LayoutProps& p) { p.gap = v; }); }
    Derived& rowGap(float v) { return layout([&](LayoutProps& p) { p.rowGap = v; }); }
    Derived& columnGap(float v) { return layout([&](LayoutProps& p) { p.columnGap = v; }); }

    Derived& positionLeft(float v) { return layout([&](LayoutProps& p) { p.positionLeft = v; }); }
    Derived& positionTop(float v) { return layout([&](LayoutProps& p) { p.positionTop = v; }); }
    Derived& positionRight(float v) { return layout([&](LayoutProps& p) { p.positionRight = v; }); }
    Derived& positionBottom(float v) {
        return layout([&](LayoutProps& p) { p.positionBottom = v; });
    }

    Derived& aspectRatio(float v) { return layout([&](LayoutProps& p) { p.aspectRatio = v; }); }

    // --- Event props (work on any primitive) ---
    Derived& onClick(std::function<void()> fn) {
        return event([&](EventProps& p) { p.onClick = std::move(fn); });
    }
    Derived& onRightClick(std::function<void()> fn) {
        return event([&](EventProps& p) { p.onRightClick = std::move(fn); });
    }
    Derived& onMouseDown(std::function<void()> fn) {
        return event([&](EventProps& p) { p.onMouseDown = std::move(fn); });
    }
    Derived& onHover(std::function<void(bool)> fn) {
        return event([&](EventProps& p) { p.onHover = std::move(fn); });
    }
    Derived& onFocus(std::function<void(bool)> fn) {
        return event([&](EventProps& p) { p.onFocus = std::move(fn); });
    }
    Derived& onScroll(std::function<void(float, float)> fn) {
        return event([&](EventProps& p) { p.onScroll = std::move(fn); });
    }
    Derived& onKeyDown(std::function<void(int, uint16_t)> fn) {
        return event([&](EventProps& p) { p.onKeyDown = std::move(fn); });
    }
    Derived& onKeyUp(std::function<void(int, uint16_t)> fn) {
        return event([&](EventProps& p) { p.onKeyUp = std::move(fn); });
    }

    // --- Conversion seam: builders compose into the VNode/Child trees ---
    operator VNode() const& { return node_; }
    operator VNode() && { return std::move(node_); }
    operator Child() const& { return Child{node_}; }
    operator Child() && { return Child{std::move(node_)}; }

protected:
    VNode node_;

    Derived& self() { return static_cast<Derived&>(*this); }

    // Universal accessors: every props alternative inherits LayoutProps/EventProps.
    template<typename F>
    Derived& layout(F&& f) {
        std::visit([&](auto& p) { f(static_cast<LayoutProps&>(p)); }, node_.props);
        return self();
    }
    template<typename F>
    Derived& event(F&& f) {
        std::visit([&](auto& p) { f(static_cast<EventProps&>(p)); }, node_.props);
        return self();
    }
};

class BoxBuilder : public BuilderBase<BoxBuilder> {
public:
    BoxBuilder() { node_.props = BoxProps{}; }

    BoxBuilder& backgroundColor(uint32_t v) { return p([&](BoxProps& b) { b.backgroundColor = v; }); }
    BoxBuilder& borderRadius(float v) { return p([&](BoxProps& b) { b.borderRadius = v; }); }
    BoxBuilder& borderColor(uint32_t v) { return p([&](BoxProps& b) { b.borderColor = v; }); }
    BoxBuilder& borderWidth(float v) { return p([&](BoxProps& b) { b.borderWidth = v; }); }
    BoxBuilder& hoverStyle(BoxStyle s) { return p([&](BoxProps& b) { b.hoverStyle = std::move(s); }); }
    BoxBuilder& focusStyle(BoxStyle s) { return p([&](BoxProps& b) { b.focusStyle = std::move(s); }); }

private:
    template<typename F>
    BoxBuilder& p(F&& f) {
        f(std::get<BoxProps>(node_.props));
        return *this;
    }
};

class TextBuilder : public BuilderBase<TextBuilder> {
public:
    explicit TextBuilder(std::string content) {
        TextProps t;
        t.text = std::move(content);
        node_.props = std::move(t);
    }

    TextBuilder& text(std::string v) { return p([&](TextProps& t) { t.text = std::move(v); }); }
    TextBuilder& fontSize(float v) { return p([&](TextProps& t) { t.fontSize = v; }); }
    TextBuilder& color(uint32_t v) { return p([&](TextProps& t) { t.color = v; }); }
    TextBuilder& hoverStyle(TextStyle s) {
        return p([&](TextProps& t) { t.hoverStyle = std::move(s); });
    }
    TextBuilder& focusStyle(TextStyle s) {
        return p([&](TextProps& t) { t.focusStyle = std::move(s); });
    }

private:
    template<typename F>
    TextBuilder& p(F&& f) {
        f(std::get<TextProps>(node_.props));
        return *this;
    }
};

class InputBuilder : public BuilderBase<InputBuilder> {
public:
    InputBuilder() { node_.props = InputProps{}; }

    InputBuilder& value(std::string v) { return p([&](InputProps& i) { i.value = std::move(v); }); }
    InputBuilder& placeholder(std::string v) {
        return p([&](InputProps& i) { i.placeholder = std::move(v); });
    }
    InputBuilder& password(bool v) { return p([&](InputProps& i) { i.password = v; }); }
    InputBuilder& fontSize(float v) { return p([&](InputProps& i) { i.fontSize = v; }); }
    InputBuilder& color(uint32_t v) { return p([&](InputProps& i) { i.color = v; }); }
    InputBuilder& backgroundColor(uint32_t v) {
        return p([&](InputProps& i) { i.backgroundColor = v; });
    }
    InputBuilder& borderColor(uint32_t v) { return p([&](InputProps& i) { i.borderColor = v; }); }
    InputBuilder& borderWidth(float v) { return p([&](InputProps& i) { i.borderWidth = v; }); }
    InputBuilder& borderRadius(float v) { return p([&](InputProps& i) { i.borderRadius = v; }); }
    InputBuilder& onChange(std::function<void(const std::string&)> fn) {
        return p([&](InputProps& i) { i.onChange = std::move(fn); });
    }
    InputBuilder& onSubmit(std::function<void()> fn) {
        return p([&](InputProps& i) { i.onSubmit = std::move(fn); });
    }
    InputBuilder& autoFocus(bool v = true) { return p([&](InputProps& i) { i.autoFocus = v; }); }
    InputBuilder& hoverStyle(InputStyle s) {
        return p([&](InputProps& i) { i.hoverStyle = std::move(s); });
    }
    InputBuilder& focusStyle(InputStyle s) {
        return p([&](InputProps& i) { i.focusStyle = std::move(s); });
    }

private:
    template<typename F>
    InputBuilder& p(F&& f) {
        f(std::get<InputProps>(node_.props));
        return *this;
    }
};

class ScrollBuilder : public BuilderBase<ScrollBuilder> {
public:
    ScrollBuilder() { node_.props = ScrollProps{}; }

    ScrollBuilder& backgroundColor(uint32_t v) {
        return p([&](ScrollProps& s) { s.backgroundColor = v; });
    }
    ScrollBuilder& borderRadius(float v) { return p([&](ScrollProps& s) { s.borderRadius = v; }); }
    ScrollBuilder& borderColor(uint32_t v) { return p([&](ScrollProps& s) { s.borderColor = v; }); }
    ScrollBuilder& borderWidth(float v) { return p([&](ScrollProps& s) { s.borderWidth = v; }); }
    ScrollBuilder& hoverStyle(BoxStyle s) {
        return p([&](ScrollProps& sp) { sp.hoverStyle = std::move(s); });
    }
    ScrollBuilder& focusStyle(BoxStyle s) {
        return p([&](ScrollProps& sp) { sp.focusStyle = std::move(s); });
    }

private:
    template<typename F>
    ScrollBuilder& p(F&& f) {
        f(std::get<ScrollProps>(node_.props));
        return *this;
    }
};

class CanvasBuilder : public BuilderBase<CanvasBuilder> {
public:
    explicit CanvasBuilder(CanvasDrawFn draw) {
        CanvasProps c;
        c.draw = std::move(draw);
        node_.props = std::move(c);
    }
    // Canvas has no primitive-specific setters beyond construction.
};

// --- Factory functions ---

// Box with children (accepts both VNode and Component via Child variant)
inline BoxBuilder Box(std::vector<Child> children) {
    BoxBuilder b;
    b.setChildren(std::move(children));
    return b;
}

// Box with single child (VNode)
inline BoxBuilder Box(VNode child) {
    return Box(std::vector<Child>{std::move(child)});
}

// Box with single child (Component)
inline BoxBuilder Box(Component child) {
    return Box(std::vector<Child>{std::move(child)});
}

// Empty box
inline BoxBuilder Box() {
    return BoxBuilder{};
}

// Text
inline TextBuilder Text(std::string content) {
    return TextBuilder{std::move(content)};
}

// Input (controlled component)
inline InputBuilder Input() {
    return InputBuilder{};
}

// Scroll: scrollable container with clipping
// Child content can exceed container bounds; overflow is clipped and scrollable
inline ScrollBuilder Scroll(VNode child) {
    ScrollBuilder b;
    b.setChildren(std::vector<Child>{std::move(child)});
    return b;
}

inline ScrollBuilder Scroll(Component child) {
    ScrollBuilder b;
    b.setChildren(std::vector<Child>{std::move(child)});
    return b;
}

inline ScrollBuilder Scroll(std::vector<Child> children) {
    ScrollBuilder b;
    b.setChildren(std::move(children));
    return b;
}

// Canvas: custom drawing primitive
// The draw function receives (void* ctx, float width, float height).
// ctx is renderer-specific (e.g., NVGcontext* for NvgRenderer).
// Draw relative to (0,0) - the renderer handles positioning.
inline CanvasBuilder Canvas(CanvasDrawFn draw) {
    return CanvasBuilder{std::move(draw)};
}

// --- Helpers ---

inline BoxBuilder Row(std::vector<Child> children) {
    return Box(std::move(children)).flexDirection(FlexDirection::Row);
}

inline BoxBuilder Column(std::vector<Child> children) {
    return Box(std::move(children)).flexDirection(FlexDirection::Column);
}

inline BoxBuilder Spacer() {
    return Box().flexGrow(1);
}

inline BoxBuilder Gap(float size) {
    return Box().width(size).height(size);
}

// Conditional rendering
inline VNode When(bool condition, VNode node) {
    if (condition)
        return node;
    return VNode::empty();
}

inline VNode If(bool condition, VNode ifTrue, VNode ifFalse) {
    return condition ? std::move(ifTrue) : std::move(ifFalse);
}

// Assign the per-item key onto a Child (VNode or Component) in place.
template <typename T, typename KeyFn>
inline void applyListKey(Child& child, KeyFn& keyFn, const T& item) {
    std::visit(
        [&](auto& node) {
            if constexpr (std::is_same_v<std::invoke_result_t<KeyFn, const T&>, std::string>) {
                node.key = keyFn(item);
            } else {
                node.intKey = static_cast<int64_t>(keyFn(item));
            }
        },
        child);
}

// Collect keyed children from a render function. The render function may return
// a VNode, a Component, or any per-primitive builder (which converts to either),
// so results are normalized into the Child variant before keying.
template <typename T, typename KeyFn, typename RenderFn>
inline std::vector<Child> collectListChildren(const std::vector<T>& items, KeyFn& keyFn,
                                              RenderFn& renderFn) {
    std::vector<Child> children;
    children.reserve(items.size());
    for (const auto& item : items) {
        Child child = renderFn(item);
        applyListKey(child, keyFn, item);
        children.push_back(std::move(child));
    }
    return children;
}

// Keyed list helper - works with VNode, Component, and builder render functions
template <typename T, typename KeyFn, typename RenderFn>
VNode List(const std::vector<T>& items, KeyFn keyFn, RenderFn renderFn) {
    return Box(collectListChildren(items, keyFn, renderFn))
        .flexDirection(FlexDirection::Column);
}

// Horizontal list variant
template <typename T, typename KeyFn, typename RenderFn>
VNode HList(const std::vector<T>& items, KeyFn keyFn, RenderFn renderFn) {
    return Box(collectListChildren(items, keyFn, renderFn)).flexDirection(FlexDirection::Row);
}

}  // namespace yui
