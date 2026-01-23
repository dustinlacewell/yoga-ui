#pragma once

#include "Props.hpp"

#include <string>
#include <vector>

namespace yui {

enum class PrimitiveType { Box, Text, Input, Scroll, Canvas };

struct VNode {
    static constexpr int64_t NO_INT_KEY = INT64_MIN;

    PrimitiveType type;
    std::string key;
    int64_t intKey = NO_INT_KEY;
    PropsVariant props;
    std::vector<VNode> children;
    bool isEmpty = false;

    // Empty node (skipped by reconciler)
    static VNode empty() {
        VNode n;
        n.type = PrimitiveType::Box;
        n.isEmpty = true;
        return n;
    }

    // Key helpers
    bool hasKey() const { return intKey != NO_INT_KEY || !key.empty(); }
    bool hasIntKey() const { return intKey != NO_INT_KEY; }
    bool hasStringKey() const { return !key.empty(); }

    // Key for reconciliation (string)
    VNode& setKey(std::string k) {
        key = std::move(k);
        return *this;
    }

    // Key for reconciliation (integer - more efficient)
    VNode& setKey(int64_t k) {
        intKey = k;
        return *this;
    }

    // --- Layout props (work on any primitive) ---
    VNode& width(float v) {
        layoutProps().width = v;
        return *this;
    }
    VNode& height(float v) {
        layoutProps().height = v;
        return *this;
    }
    VNode& minWidth(float v) {
        layoutProps().minWidth = v;
        return *this;
    }
    VNode& minHeight(float v) {
        layoutProps().minHeight = v;
        return *this;
    }
    VNode& maxWidth(float v) {
        layoutProps().maxWidth = v;
        return *this;
    }
    VNode& maxHeight(float v) {
        layoutProps().maxHeight = v;
        return *this;
    }

    VNode& flexGrow(float v) {
        layoutProps().flexGrow = v;
        return *this;
    }
    VNode& flexShrink(float v) {
        layoutProps().flexShrink = v;
        return *this;
    }
    VNode& flexBasis(float v) {
        layoutProps().flexBasis = v;
        return *this;
    }
    VNode& flexDirection(FlexDirection v) {
        layoutProps().flexDirection = v;
        return *this;
    }
    VNode& flexWrap(FlexWrap v) {
        layoutProps().flexWrap = v;
        return *this;
    }
    VNode& justifyContent(JustifyContent v) {
        layoutProps().justifyContent = v;
        return *this;
    }
    VNode& alignItems(AlignItems v) {
        layoutProps().alignItems = v;
        return *this;
    }
    VNode& alignContent(AlignContent v) {
        layoutProps().alignContent = v;
        return *this;
    }
    VNode& alignSelf(AlignSelf v) {
        layoutProps().alignSelf = v;
        return *this;
    }
    VNode& positionType(PositionType v) {
        layoutProps().positionType = v;
        return *this;
    }
    VNode& display(Display v) {
        layoutProps().display = v;
        return *this;
    }

    VNode& padding(float v) {
        layoutProps().padding = v;
        return *this;
    }
    VNode& paddingTop(float v) {
        layoutProps().paddingTop = v;
        return *this;
    }
    VNode& paddingRight(float v) {
        layoutProps().paddingRight = v;
        return *this;
    }
    VNode& paddingBottom(float v) {
        layoutProps().paddingBottom = v;
        return *this;
    }
    VNode& paddingLeft(float v) {
        layoutProps().paddingLeft = v;
        return *this;
    }

    VNode& margin(float v) {
        layoutProps().margin = v;
        return *this;
    }
    VNode& marginTop(float v) {
        layoutProps().marginTop = v;
        return *this;
    }
    VNode& marginRight(float v) {
        layoutProps().marginRight = v;
        return *this;
    }
    VNode& marginBottom(float v) {
        layoutProps().marginBottom = v;
        return *this;
    }
    VNode& marginLeft(float v) {
        layoutProps().marginLeft = v;
        return *this;
    }

    VNode& gap(float v) {
        layoutProps().gap = v;
        return *this;
    }
    VNode& rowGap(float v) {
        layoutProps().rowGap = v;
        return *this;
    }
    VNode& columnGap(float v) {
        layoutProps().columnGap = v;
        return *this;
    }

    // Absolute positioning
    VNode& positionLeft(float v) {
        layoutProps().positionLeft = v;
        return *this;
    }
    VNode& positionTop(float v) {
        layoutProps().positionTop = v;
        return *this;
    }
    VNode& positionRight(float v) {
        layoutProps().positionRight = v;
        return *this;
    }
    VNode& positionBottom(float v) {
        layoutProps().positionBottom = v;
        return *this;
    }

    VNode& aspectRatio(float v) {
        layoutProps().aspectRatio = v;
        return *this;
    }

    // --- Event props (work on any primitive) ---
    VNode& onClick(std::function<void()> fn) {
        eventProps().onClick = std::move(fn);
        return *this;
    }
    VNode& onRightClick(std::function<void()> fn) {
        eventProps().onRightClick = std::move(fn);
        return *this;
    }
    VNode& onHover(std::function<void(bool)> fn) {
        eventProps().onHover = std::move(fn);
        return *this;
    }
    VNode& onFocus(std::function<void(bool)> fn) {
        eventProps().onFocus = std::move(fn);
        return *this;
    }
    VNode& onScroll(std::function<void(float, float)> fn) {
        eventProps().onScroll = std::move(fn);
        return *this;
    }
    VNode& onKeyDown(std::function<void(int, uint16_t)> fn) {
        eventProps().onKeyDown = std::move(fn);
        return *this;
    }
    VNode& onKeyUp(std::function<void(int, uint16_t)> fn) {
        eventProps().onKeyUp = std::move(fn);
        return *this;
    }

    // --- Visual style props (work on Box and Input) ---
    VNode& backgroundColor(uint32_t v);
    VNode& borderRadius(float v);
    VNode& borderColor(uint32_t v);
    VNode& borderWidth(float v);

    // --- Text-specific ---
    VNode& text(std::string v) {
        textProps().text = std::move(v);
        return *this;
    }
    VNode& fontSize(float v);  // Works on Text and Input
    VNode& color(uint32_t v);  // Works on Text and Input

    // --- Input-specific ---
    VNode& value(std::string* v) {
        inputProps().value = v;
        return *this;
    }
    VNode& placeholder(std::string v) {
        inputProps().placeholder = std::move(v);
        return *this;
    }
    VNode& password(bool v) {
        inputProps().password = v;
        return *this;
    }
    VNode& onChange(std::function<void(const std::string&)> fn) {
        inputProps().onChange = std::move(fn);
        return *this;
    }
    VNode& onSubmit(std::function<void()> fn) {
        inputProps().onSubmit = std::move(fn);
        return *this;
    }

    // --- State-based style overrides ---
    VNode& hoverStyle(BoxStyle style);
    VNode& hoverStyle(TextStyle style);
    VNode& hoverStyle(InputStyle style);
    VNode& focusStyle(BoxStyle style);
    VNode& focusStyle(TextStyle style);
    VNode& focusStyle(InputStyle style);

    // --- Children (for chaining after Box()) ---
    VNode& setChildren(std::vector<VNode> c) {
        children = std::move(c);
        return *this;
    }

private:
    // Accessors that work across variant types (all inherit LayoutProps/EventProps)
    LayoutProps& layoutProps() {
        return std::visit([](auto& p) -> LayoutProps& { return p; }, props);
    }
    EventProps& eventProps() {
        return std::visit([](auto& p) -> EventProps& { return p; }, props);
    }

    // Type-specific accessors (assert correct type)
    BoxProps& boxProps() { return std::get<BoxProps>(props); }
    TextProps& textProps() { return std::get<TextProps>(props); }
    InputProps& inputProps() { return std::get<InputProps>(props); }
    ScrollProps& scrollProps() { return std::get<ScrollProps>(props); }
    CanvasProps& canvasProps() { return std::get<CanvasProps>(props); }
};

// --- Factory functions ---

// Box with children
inline VNode Box(std::vector<VNode> children) {
    VNode n;
    n.type = PrimitiveType::Box;
    n.props = BoxProps{};
    n.children = std::move(children);
    return n;
}

// Box with single child
inline VNode Box(VNode child) {
    return Box(std::vector<VNode>{std::move(child)});
}

// Empty box
inline VNode Box() {
    return Box(std::vector<VNode>{});
}

// Text
inline VNode Text(std::string content) {
    VNode n;
    n.type = PrimitiveType::Text;
    TextProps p;
    p.text = std::move(content);
    n.props = std::move(p);
    return n;
}

// Input
inline VNode Input(std::string* value) {
    VNode n;
    n.type = PrimitiveType::Input;
    InputProps p;
    p.value = value;
    n.props = std::move(p);
    return n;
}

// Scroll: scrollable container with clipping
// Child content can exceed container bounds; overflow is clipped and scrollable
inline VNode Scroll(VNode child) {
    VNode n;
    n.type = PrimitiveType::Scroll;
    n.props = ScrollProps{};
    n.children.push_back(std::move(child));
    return n;
}

inline VNode Scroll(std::vector<VNode> children) {
    VNode n;
    n.type = PrimitiveType::Scroll;
    n.props = ScrollProps{};
    n.children = std::move(children);
    return n;
}

// Canvas: custom drawing primitive
// The draw function receives (void* ctx, float width, float height).
// ctx is renderer-specific (e.g., NVGcontext* for NvgRenderer).
// Draw relative to (0,0) - the renderer handles positioning.
inline VNode Canvas(CanvasDrawFn draw) {
    VNode n;
    n.type = PrimitiveType::Canvas;
    CanvasProps p;
    p.draw = std::move(draw);
    n.props = std::move(p);
    return n;
}

// --- Helpers ---

inline VNode Row(std::vector<VNode> children) {
    return Box(std::move(children)).flexDirection(FlexDirection::Row);
}

inline VNode Column(std::vector<VNode> children) {
    return Box(std::move(children)).flexDirection(FlexDirection::Column);
}

inline VNode Spacer() {
    return Box().flexGrow(1);
}

inline VNode Gap(float size) {
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

// Keyed list helper
template <typename T, typename KeyFn, typename RenderFn>
VNode List(const std::vector<T>& items, KeyFn keyFn, RenderFn renderFn) {
    std::vector<VNode> children;
    children.reserve(items.size());
    for (const auto& item : items) {
        auto node = renderFn(item);
        // Convert key to string
        if constexpr (std::is_same_v<std::invoke_result_t<KeyFn, const T&>, std::string>) {
            node.key = keyFn(item);
        } else {
            node.key = std::to_string(keyFn(item));
        }
        children.push_back(std::move(node));
    }
    return Box(std::move(children)).flexDirection(FlexDirection::Column);
}

// Horizontal list variant
template <typename T, typename KeyFn, typename RenderFn>
VNode HList(const std::vector<T>& items, KeyFn keyFn, RenderFn renderFn) {
    std::vector<VNode> children;
    children.reserve(items.size());
    for (const auto& item : items) {
        auto node = renderFn(item);
        if constexpr (std::is_same_v<std::invoke_result_t<KeyFn, const T&>, std::string>) {
            node.key = keyFn(item);
        } else {
            node.key = std::to_string(keyFn(item));
        }
        children.push_back(std::move(node));
    }
    return Box(std::move(children)).flexDirection(FlexDirection::Row);
}

}  // namespace yui
