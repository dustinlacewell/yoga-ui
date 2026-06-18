#include <yui/nvg/NvgRenderer.hpp>

#include <yui/nvg/detail/NvgScopes.hpp>

#include <nanovg.h>
#include <chrono>
#include <exception>

namespace yui {
namespace nvg {

NvgRenderer::NvgRenderer(NVGcontext* vg, int fontId, ErrorHandler onError)
    : vg_(vg), fontId_(fontId), onError_(std::move(onError)) {}

void NvgRenderer::reportError(std::string_view where, const std::exception* eOrNull) noexcept {
    if (!onError_)
        return;
    try {
        onError_(where, eOrNull);
    } catch (...) {
    }
}

Size NvgRenderer::measure(const std::string& text, float fontSize, float maxWidth) const {
    if (!vg_) {
        return fallbackMeasure(text, fontSize, maxWidth);
    }

    nvgFontSize(vg_, fontSize);
    if (fontId_ >= 0) {
        nvgFontFaceId(vg_, fontId_);
    } else {
        nvgFontFace(vg_, "default");
    }

    float bounds[4];
    float width = nvgTextBounds(vg_, 0, 0, text.c_str(), nullptr, bounds);
    float height = bounds[3] - bounds[1];

    if (maxWidth > 0 && width > maxWidth) {
        width = maxWidth;
    }

    return {width, height};
}

void NvgRenderer::render(Node* root) noexcept {
    if (!vg_ || !root)
        return;

    // Backstop: a draw exception is already isolated per-Canvas in drawCanvas, but
    // anything else thrown during the walk must not escape into the draw-time C
    // boundary. The per-Canvas RAII scopes (landed in the prior commit) have
    // already restored NanoVG state on unwind, so catching here leaves the context
    // balanced.
    try {
        DrawContext ctx{0, 0};
        drawNode(ctx, root);
    } catch (const std::exception& e) {
        reportError("NvgRenderer::render", &e);
    } catch (...) {
        reportError("NvgRenderer::render", nullptr);
    }
}

void NvgRenderer::drawNode(DrawContext& ctx, Node* node) {
    float x = ctx.offsetX + node->layout.left;
    float y = ctx.offsetY + node->layout.top;

    switch (node->type()) {
    case PrimitiveType::Box:
        drawBox(ctx, static_cast<BoxNode*>(node));
        break;
    case PrimitiveType::Text:
        drawText(ctx, static_cast<TextNode*>(node));
        break;
    case PrimitiveType::Input:
        drawInput(ctx, static_cast<InputNode*>(node));
        break;
    case PrimitiveType::Scroll:
        drawScroll(ctx, static_cast<ScrollNode*>(node));
        return;  // drawScroll handles its own children
    case PrimitiveType::Canvas:
        drawCanvas(ctx, static_cast<CanvasNode*>(node));
        break;
    }

    DrawContext childCtx = ctx;
    childCtx.offsetX = x;
    childCtx.offsetY = y;

    for (auto& child : node->children) {
        drawNode(childCtx, child.get());
    }
}

void NvgRenderer::drawBox(DrawContext& ctx, BoxNode* node) {
    float x = ctx.offsetX + node->layout.left;
    float y = ctx.offsetY + node->layout.top;
    float w = node->layout.width;
    float h = node->layout.height;

    auto& p = node->props;

    // Resolve styles: base < hover < focus
    auto bg = p.backgroundColor;
    auto border = p.borderColor;
    auto borderW = p.borderWidth;
    float radius = p.borderRadius.value_or(0);

    if (node->hovered && p.hoverStyle) {
        if (p.hoverStyle->backgroundColor)
            bg = p.hoverStyle->backgroundColor;
        if (p.hoverStyle->borderColor)
            border = p.hoverStyle->borderColor;
        if (p.hoverStyle->borderWidth)
            borderW = p.hoverStyle->borderWidth;
        if (p.hoverStyle->borderRadius)
            radius = *p.hoverStyle->borderRadius;
    }
    if (node->focused && p.focusStyle) {
        if (p.focusStyle->backgroundColor)
            bg = p.focusStyle->backgroundColor;
        if (p.focusStyle->borderColor)
            border = p.focusStyle->borderColor;
        if (p.focusStyle->borderWidth)
            borderW = p.focusStyle->borderWidth;
        if (p.focusStyle->borderRadius)
            radius = *p.focusStyle->borderRadius;
    }

    if (bg) {
        uint32_t c = *bg;
        nvgBeginPath(vg_);
        if (radius > 0) {
            nvgRoundedRect(vg_, x, y, w, h, radius);
        } else {
            nvgRect(vg_, x, y, w, h);
        }
        nvgFillColor(vg_, nvgRGBA((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
        nvgFill(vg_);
    }

    if (borderW && *borderW > 0 && border) {
        uint32_t c = *border;
        nvgBeginPath(vg_);
        if (radius > 0) {
            nvgRoundedRect(vg_, x, y, w, h, radius);
        } else {
            nvgRect(vg_, x, y, w, h);
        }
        nvgStrokeColor(vg_, nvgRGBA((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
        nvgStrokeWidth(vg_, *borderW);
        nvgStroke(vg_);
    }
}

void NvgRenderer::drawText(DrawContext& ctx, TextNode* node) {
    float x = ctx.offsetX + node->layout.left;
    float y = ctx.offsetY + node->layout.top;

    auto& p = node->props;

    // Resolve styles: base < hover < focus
    float fontSize = p.fontSize.value_or(12.0f);
    uint32_t c = p.color.value_or(0xFFFFFFFF);

    if (node->hovered && p.hoverStyle) {
        if (p.hoverStyle->fontSize)
            fontSize = *p.hoverStyle->fontSize;
        if (p.hoverStyle->color)
            c = *p.hoverStyle->color;
    }
    if (node->focused && p.focusStyle) {
        if (p.focusStyle->fontSize)
            fontSize = *p.focusStyle->fontSize;
        if (p.focusStyle->color)
            c = *p.focusStyle->color;
    }

    nvgFontSize(vg_, fontSize);
    if (fontId_ >= 0) {
        nvgFontFaceId(vg_, fontId_);
    } else {
        nvgFontFace(vg_, "default");
    }
    nvgFillColor(vg_, nvgRGBA((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
    nvgTextAlign(vg_, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgText(vg_, x, y, p.text.c_str(), nullptr);
}

void NvgRenderer::drawScroll(DrawContext& ctx, ScrollNode* node) {
    float x = ctx.offsetX + node->layout.left;
    float y = ctx.offsetY + node->layout.top;
    float w = node->layout.width;
    float h = node->layout.height;

    auto& p = node->props;

    // Resolve styles: base < hover < focus
    auto bg = p.backgroundColor;
    auto border = p.borderColor;
    auto borderW = p.borderWidth;
    float radius = p.borderRadius.value_or(0);

    if (node->hovered && p.hoverStyle) {
        if (p.hoverStyle->backgroundColor)
            bg = p.hoverStyle->backgroundColor;
        if (p.hoverStyle->borderColor)
            border = p.hoverStyle->borderColor;
        if (p.hoverStyle->borderWidth)
            borderW = p.hoverStyle->borderWidth;
        if (p.hoverStyle->borderRadius)
            radius = *p.hoverStyle->borderRadius;
    }
    if (node->focused && p.focusStyle) {
        if (p.focusStyle->backgroundColor)
            bg = p.focusStyle->backgroundColor;
        if (p.focusStyle->borderColor)
            border = p.focusStyle->borderColor;
        if (p.focusStyle->borderWidth)
            borderW = p.focusStyle->borderWidth;
        if (p.focusStyle->borderRadius)
            radius = *p.focusStyle->borderRadius;
    }

    if (bg) {
        uint32_t c = *bg;
        nvgBeginPath(vg_);
        if (radius > 0) {
            nvgRoundedRect(vg_, x, y, w, h, radius);
        } else {
            nvgRect(vg_, x, y, w, h);
        }
        nvgFillColor(vg_, nvgRGBA((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
        nvgFill(vg_);
    }

    if (borderW && *borderW > 0 && border) {
        uint32_t c = *border;
        nvgBeginPath(vg_);
        if (radius > 0) {
            nvgRoundedRect(vg_, x, y, w, h, radius);
        } else {
            nvgRect(vg_, x, y, w, h);
        }
        nvgStrokeColor(vg_, nvgRGBA((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
        nvgStrokeWidth(vg_, *borderW);
        nvgStroke(vg_);
    }

    detail::NvgSaveScope saveScope(vg_);
    nvgTranslate(vg_, x, y);
    nvgIntersectScissor(vg_, 0, 0, w, h);

    DrawContext childCtx = ctx;
    childCtx.offsetX = -node->scrollOffsetX;
    childCtx.offsetY = -node->scrollOffsetY;

    for (auto& child : node->children) {
        drawNode(childCtx, child.get());
    }
}

void NvgRenderer::drawCanvas(DrawContext& ctx, CanvasNode* node) {
    if (!node->props.draw)
        return;

    float x = ctx.offsetX + node->layout.left;
    float y = ctx.offsetY + node->layout.top;
    float w = node->layout.width;
    float h = node->layout.height;

    // The save scope restores NanoVG state on any exit, including a throw from the
    // user draw callback. Isolate that throw and continue rendering siblings.
    detail::NvgSaveScope saveScope(vg_);
    nvgTranslate(vg_, x, y);
    try {
        node->props.draw(vg_, w, h);
    } catch (const std::exception& e) {
        reportError("draw", &e);
    } catch (...) {
        reportError("draw", nullptr);
    }
}

void NvgRenderer::drawInput(DrawContext& ctx, InputNode* node) {
    float x = ctx.offsetX + node->layout.left;
    float y = ctx.offsetY + node->layout.top;
    float w = node->layout.width;
    float h = node->layout.height;

    auto& p = node->props;

    // Default styles for Input
    constexpr uint32_t DEFAULT_BG = 0x282828FF;
    constexpr uint32_t DEFAULT_BORDER = 0x505050FF;
    constexpr uint32_t DEFAULT_HOVER_BORDER = 0x707070FF;
    constexpr uint32_t DEFAULT_FOCUS_BORDER = 0x4a9fffFF;

    // Resolve styles: base < hover < focus
    uint32_t bg = p.backgroundColor.value_or(DEFAULT_BG);
    uint32_t border = p.borderColor.value_or(DEFAULT_BORDER);
    float borderW = p.borderWidth.value_or(1.0f);
    float radius = p.borderRadius.value_or(0.0f);
    float fontSize = p.fontSize.value_or(12.0f);
    uint32_t c = p.color.value_or(0xFFFFFFFF);

    if (node->hovered) {
        border = p.borderColor.value_or(DEFAULT_HOVER_BORDER);
        if (p.hoverStyle) {
            if (p.hoverStyle->backgroundColor)
                bg = *p.hoverStyle->backgroundColor;
            if (p.hoverStyle->borderColor)
                border = *p.hoverStyle->borderColor;
            if (p.hoverStyle->borderWidth)
                borderW = *p.hoverStyle->borderWidth;
            if (p.hoverStyle->borderRadius)
                radius = *p.hoverStyle->borderRadius;
            if (p.hoverStyle->fontSize)
                fontSize = *p.hoverStyle->fontSize;
            if (p.hoverStyle->color)
                c = *p.hoverStyle->color;
        }
    }
    if (node->focused) {
        border = p.borderColor.value_or(DEFAULT_FOCUS_BORDER);
        if (p.focusStyle) {
            if (p.focusStyle->backgroundColor)
                bg = *p.focusStyle->backgroundColor;
            if (p.focusStyle->borderColor)
                border = *p.focusStyle->borderColor;
            if (p.focusStyle->borderWidth)
                borderW = *p.focusStyle->borderWidth;
            if (p.focusStyle->borderRadius)
                radius = *p.focusStyle->borderRadius;
            if (p.focusStyle->fontSize)
                fontSize = *p.focusStyle->fontSize;
            if (p.focusStyle->color)
                c = *p.focusStyle->color;
        }
    }

    // Background
    nvgBeginPath(vg_);
    if (radius > 0) {
        nvgRoundedRect(vg_, x, y, w, h, radius);
    } else {
        nvgRect(vg_, x, y, w, h);
    }
    nvgFillColor(vg_, nvgRGBA((bg >> 24) & 0xFF, (bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF));
    nvgFill(vg_);

    // Border
    if (borderW > 0) {
        nvgBeginPath(vg_);
        if (radius > 0) {
            nvgRoundedRect(vg_, x, y, w, h, radius);
        } else {
            nvgRect(vg_, x, y, w, h);
        }
        nvgStrokeColor(vg_, nvgRGBA((border >> 24) & 0xFF, (border >> 16) & 0xFF, (border >> 8) & 0xFF, border & 0xFF));
        nvgStrokeWidth(vg_, borderW);
        nvgStroke(vg_);
    }

    // Text content - use node's displayText (synced from controlled value)
    std::string textToDraw;
    uint32_t textColor = c;
    if (!node->displayText.empty()) {
        if (p.password && *p.password) {
            textToDraw = std::string(node->displayText.length(), '*');
        } else {
            textToDraw = node->displayText;
        }
    } else if (p.placeholder) {
        textToDraw = *p.placeholder;
        textColor = 0x808080FF;
    }

    // Set up font for text and cursor measurement
    constexpr float textPad = 4.0f;
    nvgFontSize(vg_, fontSize);
    if (fontId_ >= 0) {
        nvgFontFaceId(vg_, fontId_);
    } else {
        nvgFontFace(vg_, "default");
    }
    nvgTextAlign(vg_, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

    if (!textToDraw.empty()) {
        nvgFillColor(vg_, nvgRGBA((textColor >> 24) & 0xFF, (textColor >> 16) & 0xFF, (textColor >> 8) & 0xFF,
                                  textColor & 0xFF));
        nvgText(vg_, x + textPad, y + h / 2, textToDraw.c_str(), nullptr);
    }

    // Blinking cursor when focused
    if (node->focused) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        bool cursorVisible = (ms % 1000) < 530;

        if (cursorVisible) {
            float cursorX = x + textPad;
            if (!node->displayText.empty()) {
                float bounds[4];
                nvgTextBounds(vg_, 0, 0, textToDraw.c_str(), nullptr, bounds);
                cursorX += bounds[2] - bounds[0];
            }
            float cursorTop = y + 3;
            float cursorBottom = y + h - 3;
            nvgBeginPath(vg_);
            nvgMoveTo(vg_, cursorX, cursorTop);
            nvgLineTo(vg_, cursorX, cursorBottom);
            nvgStrokeColor(vg_, nvgRGBA((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
            nvgStrokeWidth(vg_, 1.0f);
            nvgStroke(vg_);
        }
    }
}

}  // namespace nvg
}  // namespace yui
