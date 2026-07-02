// Primitive UI components for yui showcase
// Theme-aware building blocks

#pragma once

#include "State.hpp"
#include "yui/yui.hpp"

#include <functional>
#include <string>

namespace showcase {

using namespace yui;

// ============================================================================
// Typography
// ============================================================================

inline VNode Text_(const std::string& str, uint32_t color, float size = 13.0f) {
    return Text(str).fontSize(size).color(color);
}

inline VNode Heading(const std::string& str) {
    return Text_(str, theme().text, 16.0f);
}

inline VNode Label(const std::string& str) {
    return Text_(str, theme().textMuted, 12.0f);
}

inline VNode Body(const std::string& str) {
    return Text_(str, theme().text, 13.0f);
}

// ============================================================================
// Buttons
// ============================================================================

inline VNode Button(const std::string& label, std::function<void()> onClick, uint32_t bg = 0, uint32_t hoverBg = 0) {
    const auto& t = theme();
    uint32_t bgColor = bg ? bg : t.accent;
    uint32_t hoverColor = hoverBg ? hoverBg : t.accentHover;

    return Box(Text_(label, t.textOnAccent, 13.0f))
        .backgroundColor(bgColor)
        .borderRadius(t.radiusSm)
        .paddingTop(8)
        .paddingBottom(8)
        .paddingLeft(12)
        .paddingRight(12)
        .onClick(std::move(onClick))
        .hoverStyle(BoxStyle{.backgroundColor = hoverColor})
        .justifyContent(JustifyContent::Center)
        .alignItems(AlignItems::Center);
}

inline VNode SmallButton(const std::string& label, std::function<void()> onClick, uint32_t bg = 0) {
    const auto& t = theme();
    uint32_t bgColor = bg ? bg : t.surfaceAlt;

    return Box(Text_(label, t.text, 11.0f))
        .backgroundColor(bgColor)
        .borderRadius(t.radiusSm)
        .paddingTop(4)
        .paddingBottom(4)
        .paddingLeft(8)
        .paddingRight(8)
        .onClick(std::move(onClick))
        .hoverStyle(BoxStyle{.backgroundColor = t.border})
        .justifyContent(JustifyContent::Center)
        .alignItems(AlignItems::Center);
}

inline VNode IconButton(const std::string& icon, std::function<void()> onClick) {
    const auto& t = theme();
    return Box(Text_(icon, t.textMuted, 12.0f))
        .backgroundColor(t.surfaceAlt)
        .borderRadius(t.radiusSm)
        .padding(6)
        .onClick(std::move(onClick))
        .hoverStyle(BoxStyle{.backgroundColor = t.danger})
        .justifyContent(JustifyContent::Center)
        .alignItems(AlignItems::Center);
}

// ============================================================================
// Inputs
// ============================================================================

inline InputBuilder TextInput(const std::string& value, std::function<void(const std::string&)> onChange,
                              const std::string& placeholder = "", bool password = false) {
    const auto& t = theme();
    return Input()
        .value(value)
        .onChange(std::move(onChange))
        .placeholder(placeholder)
        .password(password)
        .height(32)
        .color(t.text)
        .backgroundColor(t.bg)
        .borderColor(t.border)
        .borderWidth(1)
        .borderRadius(t.radiusSm)
        .hoverStyle(InputStyle{.borderColor = t.textMuted})
        .focusStyle(InputStyle{.borderColor = t.accent});
}

inline VNode LabeledInput(const std::string& label, const std::string& value,
                          std::function<void(const std::string&)> onChange, const std::string& placeholder = "",
                          bool password = false) {
    return Column(Label(label), TextInput(value, std::move(onChange), placeholder, password)).gap(4).flexGrow(1);
}

// ============================================================================
// Containers
// ============================================================================

inline VNode Card(std::vector<Child> children) {
    const auto& t = theme();
    return Box(std::move(children))
        .backgroundColor(t.surface)
        .borderRadius(t.radiusMd)
        .borderColor(t.border)
        .borderWidth(1)
        .padding(t.padding)
        .flexDirection(FlexDirection::Column)
        .gap(t.gap);
}

inline VNode Section(const std::string& title, std::vector<Child> children) {
    std::vector<Child> content;
    content.push_back(Heading(title));
    for (auto& child : children) {
        content.push_back(std::move(child));
    }
    return Card(std::move(content));
}

// ============================================================================
// Badges & Pills
// ============================================================================

inline VNode Badge(const std::string& text, uint32_t bg) {
    const auto& t = theme();
    return Box(Text_(text, t.text, 10.0f))
        .backgroundColor(bg)
        .borderRadius(t.radiusLg)
        .paddingTop(3)
        .paddingBottom(3)
        .paddingLeft(8)
        .paddingRight(8);
}

inline VNode StatusBadge(const std::string& text, uint32_t color) {
    return Badge(text, color);
}

}  // namespace showcase
