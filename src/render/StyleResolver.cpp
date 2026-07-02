#include <yui/core/RenderDefaults.hpp>
#include <yui/render/StyleResolver.hpp>

namespace yui::render {
namespace {

void overlay(ResolvedBoxStyle& s, const BoxStyle& o) {
    if (o.backgroundColor)
        s.backgroundColor = o.backgroundColor;
    if (o.borderColor)
        s.borderColor = o.borderColor;
    if (o.borderWidth)
        s.borderWidth = *o.borderWidth;
    if (o.borderRadius)
        s.borderRadius = *o.borderRadius;
}

void overlay(ResolvedInputStyle& s, const InputStyle& o) {
    if (o.backgroundColor)
        s.backgroundColor = *o.backgroundColor;
    if (o.borderColor)
        s.borderColor = *o.borderColor;
    if (o.borderWidth)
        s.borderWidth = *o.borderWidth;
    if (o.borderRadius)
        s.borderRadius = *o.borderRadius;
    if (o.fontSize)
        s.fontSize = *o.fontSize;
    if (o.color)
        s.color = *o.color;
}

// BoxProps and ScrollProps share the same visual fields and BoxStyle overrides;
// one cascade serves both.
template <typename P>
ResolvedBoxStyle resolveBoxLike(const P& p, bool hovered, bool focused) {
    ResolvedBoxStyle s;
    s.backgroundColor = p.backgroundColor;
    s.borderColor = p.borderColor;
    s.borderWidth = p.borderWidth.value_or(0);
    s.borderRadius = p.borderRadius.value_or(0);
    if (hovered && p.hoverStyle)
        overlay(s, *p.hoverStyle);
    if (focused && p.focusStyle)
        overlay(s, *p.focusStyle);
    return s;
}

}  // namespace

ResolvedBoxStyle resolveBox(const BoxProps& p, bool hovered, bool focused) {
    return resolveBoxLike(p, hovered, focused);
}

ResolvedBoxStyle resolveBox(const ScrollProps& p, bool hovered, bool focused) {
    return resolveBoxLike(p, hovered, focused);
}

ResolvedTextStyle resolveText(const TextProps& p, bool hovered, bool focused) {
    namespace rd = render_defaults;
    ResolvedTextStyle s;
    s.fontSize = p.fontSize.value_or(rd::kDefaultFontSize);
    s.color = p.color.value_or(rd::kDefaultTextColor);
    if (hovered && p.hoverStyle) {
        if (p.hoverStyle->fontSize)
            s.fontSize = *p.hoverStyle->fontSize;
        if (p.hoverStyle->color)
            s.color = *p.hoverStyle->color;
    }
    if (focused && p.focusStyle) {
        if (p.focusStyle->fontSize)
            s.fontSize = *p.focusStyle->fontSize;
        if (p.focusStyle->color)
            s.color = *p.focusStyle->color;
    }
    return s;
}

ResolvedInputStyle resolveInput(const InputProps& p, bool hovered, bool focused) {
    namespace rd = render_defaults;
    ResolvedInputStyle s;
    s.backgroundColor = p.backgroundColor.value_or(rd::kInputBg);
    s.borderColor = p.borderColor.value_or(rd::kInputBorder);
    s.borderWidth = p.borderWidth.value_or(rd::kInputBorderWidth);
    s.borderRadius = p.borderRadius.value_or(rd::kInputBorderRadius);
    s.fontSize = p.fontSize.value_or(rd::kDefaultFontSize);
    s.color = p.color.value_or(rd::kDefaultTextColor);

    // Each state re-resolves the default border from the BASE borderColor (a
    // user-set base border suppresses the state default), then overlays the
    // user's state style. Focus runs last, so its default border beats an
    // earlier hoverStyle borderColor when both states are active.
    if (hovered) {
        s.borderColor = p.borderColor.value_or(rd::kInputHoverBorder);
        if (p.hoverStyle)
            overlay(s, *p.hoverStyle);
    }
    if (focused) {
        s.borderColor = p.borderColor.value_or(rd::kInputFocusBorder);
        if (p.focusStyle)
            overlay(s, *p.focusStyle);
    }
    return s;
}

}  // namespace yui::render
