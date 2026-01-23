#include <yui/core/VNode.hpp>

namespace yui {

VNode& VNode::backgroundColor(uint32_t v) {
    std::visit(
        [v](auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, BoxProps>) {
                p.backgroundColor = v;
            } else if constexpr (std::is_same_v<T, InputProps>) {
                p.backgroundColor = v;
            }
        },
        props);
    return *this;
}

VNode& VNode::borderRadius(float v) {
    std::visit(
        [v](auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, BoxProps>) {
                p.borderRadius = v;
            } else if constexpr (std::is_same_v<T, InputProps>) {
                p.borderRadius = v;
            }
        },
        props);
    return *this;
}

VNode& VNode::borderColor(uint32_t v) {
    std::visit(
        [v](auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, BoxProps>) {
                p.borderColor = v;
            } else if constexpr (std::is_same_v<T, InputProps>) {
                p.borderColor = v;
            }
        },
        props);
    return *this;
}

VNode& VNode::borderWidth(float v) {
    std::visit(
        [v](auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, BoxProps>) {
                p.borderWidth = v;
            } else if constexpr (std::is_same_v<T, InputProps>) {
                p.borderWidth = v;
            }
        },
        props);
    return *this;
}

VNode& VNode::fontSize(float v) {
    std::visit(
        [v](auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, TextProps>) {
                p.fontSize = v;
            } else if constexpr (std::is_same_v<T, InputProps>) {
                p.fontSize = v;
            }
            // Box: no-op (or could assert)
        },
        props);
    return *this;
}

VNode& VNode::color(uint32_t v) {
    std::visit(
        [v](auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, TextProps>) {
                p.color = v;
            } else if constexpr (std::is_same_v<T, InputProps>) {
                p.color = v;
            }
            // Box: no-op (or could assert)
        },
        props);
    return *this;
}

// --- State-based style overrides ---

VNode& VNode::hoverStyle(BoxStyle style) {
    if (auto* p = std::get_if<BoxProps>(&props)) {
        p->hoverStyle = std::move(style);
    }
    return *this;
}

VNode& VNode::hoverStyle(TextStyle style) {
    if (auto* p = std::get_if<TextProps>(&props)) {
        p->hoverStyle = std::move(style);
    }
    return *this;
}

VNode& VNode::hoverStyle(InputStyle style) {
    if (auto* p = std::get_if<InputProps>(&props)) {
        p->hoverStyle = std::move(style);
    }
    return *this;
}

VNode& VNode::focusStyle(BoxStyle style) {
    if (auto* p = std::get_if<BoxProps>(&props)) {
        p->focusStyle = std::move(style);
    }
    return *this;
}

VNode& VNode::focusStyle(TextStyle style) {
    if (auto* p = std::get_if<TextProps>(&props)) {
        p->focusStyle = std::move(style);
    }
    return *this;
}

VNode& VNode::focusStyle(InputStyle style) {
    if (auto* p = std::get_if<InputProps>(&props)) {
        p->focusStyle = std::move(style);
    }
    return *this;
}

}  // namespace yui
