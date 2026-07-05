#pragma once

#include <yui/core/ComponentContext.hpp>
#include <yui/core/Node.hpp>
#include <yui/core/VNode.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace yui::widgets {

// Button: a clickable, pressable surface. A COMPONENT (not a pure builder)
// because "pressed" is transient UI state the core does not model — there is
// no pressedStyle primitive, so the widget holds an internal `pressed` bool via
// useState and picks the fill itself. Hover is core (hoverStyle); press is not.
//
// Composition:
//
//   Component -> Box(content)
//                  .backgroundColor(disabled ? disabledBg : pressed ? pressedBg : bg)
//                  .hoverStyle({hoverBg})       <- only when enabled
//                  .cursor(disabled ? Arrow : Pointer)
//                  .onMouseDown -> setPressed(true)   (Left, enabled)
//                  .onMouseUp   -> setPressed(false)  (Left)
//                  .onClick     -> user onClick, or a consuming no-op
//
// onClick is ALWAYS attached (a consuming no-op when disabled): a handler that
// runs to completion consumes the event, so a disabled-button click never
// bubbles into a parent's onClick. The pressed fill is GUI-only.
//
// Two content paths:
//   Button("label")  -> a themed Text label (textColor/fontSize apply)
//   Button(children) -> arbitrary content (label-only setters are inert)

inline constexpr uint32_t kButtonBg = 0x3A3A3AFFu;
inline constexpr uint32_t kButtonHoverBg = 0x4A4A4AFFu;
inline constexpr uint32_t kButtonPressedBg = 0x2A2A2AFFu;
inline constexpr uint32_t kButtonDisabledBg = 0x2A2A2AFFu;
inline constexpr uint32_t kButtonText = 0xFFFFFFFFu;
inline constexpr uint32_t kButtonDisabledText = 0x808080FFu;
inline constexpr float kButtonFontSize = 14.0f;
inline constexpr float kButtonRadius = 4.0f;
inline constexpr float kButtonPad = 8.0f;

class ButtonBuilder {
public:
    // Content path: arbitrary children wrapped as the button body.
    explicit ButtonBuilder(std::vector<Child> content) : content_(std::move(content)) {}

    // Label path: a single themed Text child, built at conversion time so
    // textColor/disabledTextColor/fontSize can apply.
    explicit ButtonBuilder(std::string label) : label_(std::move(label)) {}

    ButtonBuilder& onClick(std::function<void()> fn) {
        onClick_ = std::move(fn);
        return *this;
    }

    ButtonBuilder& disabled(bool v = true) {
        disabled_ = v;
        return *this;
    }

    ButtonBuilder& backgroundColor(uint32_t v) {
        bg_ = v;
        return *this;
    }
    ButtonBuilder& hoverColor(uint32_t v) {
        hoverBg_ = v;
        return *this;
    }
    ButtonBuilder& pressedColor(uint32_t v) {
        pressedBg_ = v;
        return *this;
    }
    ButtonBuilder& disabledColor(uint32_t v) {
        disabledBg_ = v;
        return *this;
    }

    // Label-path only: color of the generated Text (inert on the content path).
    ButtonBuilder& textColor(uint32_t v) {
        textColor_ = v;
        return *this;
    }
    ButtonBuilder& disabledTextColor(uint32_t v) {
        disabledTextColor_ = v;
        return *this;
    }
    ButtonBuilder& fontSize(float v) {
        fontSize_ = v;
        return *this;
    }

    ButtonBuilder& borderRadius(float v) {
        radius_ = v;
        return *this;
    }
    ButtonBuilder& padding(float v) {
        padding_ = v;
        return *this;
    }

    // --- Conversion seam (Child-only, as a Component; see class note) ---
    operator Child() const& { return Child{build()}; }
    operator Child() && { return Child{build()}; }

private:
    Component build() const {
        // Snapshot every field into the closure: the component re-renders and
        // must not reach back into a builder that has been moved from.
        return Component([content = content_, label = label_, userOnClick = onClick_, disabled = disabled_, bg = bg_,
                          hoverBg = hoverBg_, pressedBg = pressedBg_, disabledBg = disabledBg_, textColor = textColor_,
                          disabledTextColor = disabledTextColor_, fontSize = fontSize_, radius = radius_,
                          padding = padding_](ComponentContext& ctx) -> VNode {
                   auto [pressed, setPressed] = ctx.useState<bool>(false);

                   std::vector<Child> body;
                   if (label) {
                       body.emplace_back(
                           Text(*label).color(disabled ? disabledTextColor : textColor).fontSize(fontSize));
                   } else {
                       body = content;  // copy: repeatable render
                   }

                   auto box = Box(std::move(body))
                                  .setKey("button")
                                  .backgroundColor(disabled ? disabledBg : pressed ? pressedBg : bg)
                                  .borderRadius(radius)
                                  .padding(padding)
                                  .justifyContent(JustifyContent::Center)
                                  .alignItems(AlignItems::Center)
                                  .cursor(disabled ? CursorShape::Arrow : CursorShape::Pointer)
                                  .onMouseDown([setPressed, disabled](float, float, MouseButton b, uint16_t) {
                                      if (b == MouseButton::Left && !disabled)
                                          setPressed(true);
                                  })
                                  .onMouseUp([setPressed](float, float, MouseButton b) {
                                      if (b == MouseButton::Left)
                                          setPressed(false);
                                  })
                                  .onClick(disabled ? std::function<void()>([] {})
                                                    : (userOnClick ? userOnClick : std::function<void()>([] {})));

                   // Hover fill only when enabled: a disabled button must not
                   // light up under the pointer.
                   if (!disabled)
                       box.hoverStyle(BoxStyle{.backgroundColor = hoverBg});

                   return box;
               })
            .setName("Button");
    }

    std::vector<Child> content_;
    std::optional<std::string> label_;
    std::function<void()> onClick_;
    bool disabled_ = false;
    uint32_t bg_ = kButtonBg;
    uint32_t hoverBg_ = kButtonHoverBg;
    uint32_t pressedBg_ = kButtonPressedBg;
    uint32_t disabledBg_ = kButtonDisabledBg;
    uint32_t textColor_ = kButtonText;
    uint32_t disabledTextColor_ = kButtonDisabledText;
    float fontSize_ = kButtonFontSize;
    float radius_ = kButtonRadius;
    float padding_ = kButtonPad;
};

// --- Factory functions ---

// A single string-ish argument (const char*, std::string, string_view): the
// variadic content path must defer to the dedicated std::string label overload,
// or a string literal — an exact const char*& match — would beat the
// user-conversion-to-string and land wrongly in the content path.
template <class... Cs>
inline constexpr bool is_single_string_v =
    sizeof...(Cs) == 1 && (std::is_convertible_v<Cs, std::string> && ...);

// Label path: a text-labelled button.
[[nodiscard]] inline ButtonBuilder Button(std::string label) {
    return ButtonBuilder{std::move(label)};
}

// Content path (dynamic): a runtime-built children vector.
[[nodiscard]] inline ButtonBuilder Button(std::vector<Child> content) {
    return ButtonBuilder{std::move(content)};
}

// Content path (static): each argument forwarded into a Child slot (see Box
// variadic). Guarded off both the single-vector overload (dynamic path) and the
// single-string overload (label path).
template <class... Cs, typename = std::enable_if_t<!is_single_child_vector_v<Cs...> && !is_single_string_v<Cs...>>>
[[nodiscard]] inline ButtonBuilder Button(Cs&&... cs) {
    std::vector<Child> v;
    v.reserve(sizeof...(Cs));
    (v.emplace_back(std::forward<Cs>(cs)), ...);
    return ButtonBuilder{std::move(v)};
}

}  // namespace yui::widgets
