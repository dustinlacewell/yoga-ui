#include "yui/sdl/SdlRenderer.hpp"

#include "yui/core/RenderDefaults.hpp"
#include "yui/sdl/detail/SdlScopes.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>

#include <SDL2_gfxPrimitives.h>

namespace yui {
namespace sdl {

SdlRenderer::SdlRenderer(SDL_Renderer* renderer, const std::string& fontPath, int baseFontSize,
                         ErrorHandler onError)
    : renderer_(renderer), baseFontSize_(baseFontSize), onError_(std::move(onError)) {
    // The construction-time font is the default face, registered under the empty
    // name (what an unset Text/Input `.font` resolves to).
    fonts_[std::string{}] = FontFace{fontPath, {}};
    // Pre-load the default base font.
    getFont(std::string{}, baseFontSize);
}

void SdlRenderer::registerFont(const std::string& name, const std::string& path) {
    fonts_[name] = FontFace{path, {}};
}

void SdlRenderer::reportError(std::string_view where, const std::exception* eOrNull) noexcept {
    if (!onError_)
        return;
    try {
        onError_(where, eOrNull);
    } catch (...) {
    }
}

SdlRenderer::~SdlRenderer() {
    // Close every opened (face, size) TTF_Font across all registered faces.
    for (auto& [name, face] : fonts_) {
        for (auto& [size, font] : face.sizes) {
            if (font)
                TTF_CloseFont(font);
        }
        face.sizes.clear();
    }
    fonts_.clear();
}

TTF_Font* SdlRenderer::getFont(const std::string& font, int size) const {
    if (size < 1)
        size = 1;

    // Resolve the named face; fall back to the default ("") for empty/unknown.
    auto faceIt = fonts_.find(font);
    if (faceIt == fonts_.end())
        faceIt = fonts_.find(std::string{});
    if (faceIt == fonts_.end())
        return nullptr;
    const FontFace& face = faceIt->second;

    auto it = face.sizes.find(size);
    if (it != face.sizes.end())
        return it->second;

    TTF_Font* f = TTF_OpenFont(face.path.c_str(), size);
    if (f) {
        face.sizes[size] = f;
    }
    return f;
}

Size SdlRenderer::measure(const std::string& text, float fontSize, float maxWidth,
                          const std::string& font) const {
    if (text.empty())
        return {0, fontSize};

    int size = static_cast<int>(fontSize + 0.5f);
    TTF_Font* fontPtr = getFont(font, size);
    if (!fontPtr)
        return {0, fontSize};

    int w = 0, h = 0;
    TTF_SizeUTF8(fontPtr, text.c_str(), &w, &h);

    float width = static_cast<float>(w);
    float height = static_cast<float>(h);

    if (maxWidth > 0 && width > maxWidth) {
        int lines = static_cast<int>(std::ceil(width / maxWidth));
        width = maxWidth;
        height = height * lines;
    }

    return {width, height};
}

void SdlRenderer::render(Node* root) noexcept {
    if (!root)
        return;

    // Backstop: a draw exception is already isolated per-Canvas in renderCanvas,
    // but anything else thrown during the walk must not escape into the draw-time
    // C boundary. The per-Canvas RAII scopes (clip / render-target / texture,
    // landed in the prior commit) have already restored SDL state on unwind, so
    // catching here leaves the renderer balanced.
    try {
        renderNode(root, 0, 0);
    } catch (const std::exception& e) {
        reportError("SdlRenderer::render", &e);
    } catch (...) {
        reportError("SdlRenderer::render", nullptr);
    }
}

void SdlRenderer::renderNode(Node* node, float offsetX, float offsetY) {
    float x = offsetX + node->layout.left;
    float y = offsetY + node->layout.top;

    // No default: every enumerator is cased, so -Wswitch flags a newly-added
    // PrimitiveType that has no render path (which would otherwise silently draw
    // nothing). The `handled` sentinel lets us report the only remaining failure
    // mode — a corrupt/out-of-range type value — AFTER the switch, without a
    // default that would suppress -Wswitch.
    bool handled = false;
    switch (node->type()) {
    case PrimitiveType::Box:
        renderBox(static_cast<BoxNode*>(node), x, y);
        handled = true;
        break;
    case PrimitiveType::Text:
        renderText(static_cast<TextNode*>(node), x, y);
        handled = true;
        break;
    case PrimitiveType::Input:
        renderInput(static_cast<InputNode*>(node), x, y);
        handled = true;
        break;
    case PrimitiveType::Scroll:
        renderScroll(static_cast<ScrollNode*>(node), offsetX, offsetY);
        return;  // renderScroll handles its own children
    case PrimitiveType::Canvas:
        renderCanvas(static_cast<CanvasNode*>(node), x, y);
        handled = true;
        break;
    }
    if (!handled) {  // only reachable for a corrupt type value
        reportError("SdlRenderer::renderNode: unknown PrimitiveType", nullptr);
        return;
    }

    for (auto& child : node->children) {
        renderNode(child.get(), x, y);
    }
}

void SdlRenderer::renderBox(BoxNode* node, float x, float y) {
    float w = node->layout.width;
    float h = node->layout.height;
    auto& p = node->props;

    // Resolve styles: base < hover < focus
    auto bg = p.backgroundColor;
    auto border = p.borderColor;
    auto borderW = p.borderWidth;
    auto radius = p.borderRadius.value_or(0);

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
        drawRoundedRect(x, y, w, h, radius, *bg, true);
    }

    if (borderW && border) {
        drawRoundedRect(x, y, w, h, radius, *border, false);
    }
}

void SdlRenderer::renderText(TextNode* node, float x, float y) {
    if (node->props.text.empty())
        return;

    auto& p = node->props;

    // Resolve styles: base < hover < focus
    float fontSize = p.fontSize.value_or(render_defaults::kDefaultFontSize);
    uint32_t color = p.color.value_or(render_defaults::kDefaultTextColor);

    if (node->hovered && p.hoverStyle) {
        if (p.hoverStyle->fontSize)
            fontSize = *p.hoverStyle->fontSize;
        if (p.hoverStyle->color)
            color = *p.hoverStyle->color;
    }
    if (node->focused && p.focusStyle) {
        if (p.focusStyle->fontSize)
            fontSize = *p.focusStyle->fontSize;
        if (p.focusStyle->color)
            color = *p.focusStyle->color;
    }

    drawText(node->props.text, x, y, fontSize, color, p.font.value_or(std::string{}));
}

void SdlRenderer::renderInput(InputNode* node, float x, float y) {
    float w = node->layout.width;
    float h = node->layout.height;
    auto& p = node->props;

    namespace rd = render_defaults;

    // Resolve styles: base < hover < focus. Defaults are shared with the NanoVG
    // backend (and the layout fallback for fontSize) via RenderDefaults.hpp.
    uint32_t bg = p.backgroundColor.value_or(rd::kInputBg);
    uint32_t border = p.borderColor.value_or(rd::kInputBorder);
    float borderW = p.borderWidth.value_or(rd::kInputBorderWidth);
    float radius = p.borderRadius.value_or(rd::kInputBorderRadius);
    float fontSize = p.fontSize.value_or(rd::kDefaultFontSize);
    uint32_t color = p.color.value_or(rd::kDefaultTextColor);

    if (node->hovered) {
        // Apply default hover style, then user overrides
        border = p.borderColor.value_or(rd::kInputHoverBorder);
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
                color = *p.hoverStyle->color;
        }
    }
    if (node->focused) {
        // Apply default focus style, then user overrides
        border = p.borderColor.value_or(rd::kInputFocusBorder);
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
                color = *p.focusStyle->color;
        }
    }

    drawRoundedRect(x, y, w, h, radius, bg, true);
    if (borderW > 0) {
        drawRoundedRect(x, y, w, h, radius, border, false);
    }

    const float padding = rd::kInputTextPad;

    const std::string inputFont = p.font.value_or(std::string{});

    std::string displayText;
    if (!p.value.empty()) {
        if (p.password.value_or(false)) {
            displayText = std::string(p.value.size(), '*');
        } else {
            displayText = p.value;
        }
        drawText(displayText, x + padding, y + (h - fontSize) / 2, fontSize, color, inputFont);
    } else if (p.placeholder) {
        drawText(*p.placeholder, x + padding, y + (h - fontSize) / 2, fontSize,
                 rd::kPlaceholderColor, inputFont);
    }

    // Blinking focus caret — mirrors the NanoVG backend (NvgRenderer::drawInput).
    // Shared cadence/geometry constants keep the two backends visually identical.
    // NOTE: backend-CI-verified only — the SDL path cannot be compiled or run in
    // the dev sandbox (no SDL2_ttf), so this is a careful mirror of the working
    // NanoVG caret rather than something exercised here.
    if (node->focused) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
        bool caretVisible = (ms % rd::kCaretBlinkPeriodMs) < rd::kCaretBlinkOnMs;

        if (caretVisible) {
            float caretX = x + padding;
            // Advance past the rendered text. Measure the masked display string so
            // the caret tracks password dots, matching NanoVG.
            if (!displayText.empty()) {
                if (TTF_Font* font = getFont(inputFont, static_cast<int>(fontSize + 0.5f))) {
                    int textW = 0;
                    int textH = 0;
                    TTF_SizeUTF8(font, displayText.c_str(), &textW, &textH);
                    caretX += static_cast<float>(textW);
                }
            }
            float caretTop = y + rd::kCaretInset;
            float caretBottom = y + h - rd::kCaretInset;
            uint32_t cc = color;
            vlineRGBA(renderer_, static_cast<Sint16>(caretX), static_cast<Sint16>(caretTop),
                      static_cast<Sint16>(caretBottom), static_cast<Uint8>((cc >> 24) & 0xFF),
                      static_cast<Uint8>((cc >> 16) & 0xFF), static_cast<Uint8>((cc >> 8) & 0xFF),
                      static_cast<Uint8>(cc & 0xFF));
        }
    }
}

void SdlRenderer::renderScroll(ScrollNode* node, float offsetX, float offsetY) {
    float x = offsetX + node->layout.left;
    float y = offsetY + node->layout.top;
    float w = node->layout.width;
    float h = node->layout.height;

    auto& p = node->props;

    // Resolve styles: base < hover < focus
    auto bg = p.backgroundColor;
    auto border = p.borderColor;
    auto borderW = p.borderWidth;
    auto radius = p.borderRadius.value_or(0);

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

    // Draw background
    if (bg) {
        drawRoundedRect(x, y, w, h, radius, *bg, true);
    }

    // Draw border
    if (borderW && border) {
        drawRoundedRect(x, y, w, h, radius, *border, false);
    }

    // Save current clip rect (restored on any scope exit) and apply scroll clip.
    detail::SdlClipScope clipScope(renderer_);

    SDL_Rect clipRect = {static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h)};

    // Intersect with previous clip if there was one
    if (clipScope.hadClip()) {
        const SDL_Rect& prevClip = clipScope.prevClip();
        int x1 = std::max(clipRect.x, prevClip.x);
        int y1 = std::max(clipRect.y, prevClip.y);
        int x2 = std::min(clipRect.x + clipRect.w, prevClip.x + prevClip.w);
        int y2 = std::min(clipRect.y + clipRect.h, prevClip.y + prevClip.h);
        clipRect.x = x1;
        clipRect.y = y1;
        clipRect.w = std::max(0, x2 - x1);
        clipRect.h = std::max(0, y2 - y1);
    }

    SDL_RenderSetClipRect(renderer_, &clipRect);

    // Draw children with scroll offset applied
    float childOffsetX = x - node->scrollOffsetX;
    float childOffsetY = y - node->scrollOffsetY;

    for (auto& child : node->children) {
        renderNode(child.get(), childOffsetX, childOffsetY);
    }
}

void SdlRenderer::renderCanvas(CanvasNode* node, float x, float y) {
    if (!node->props.draw)
        return;

    int w = static_cast<int>(node->layout.width);
    int h = static_cast<int>(node->layout.height);
    if (w <= 0 || h <= 0)
        return;

    // Create a texture as render target for the canvas. The RAII holder destroys
    // it on any scope exit (early return, exception, or normal fall-through).
    detail::SdlTexture target(
        SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h));
    if (!target)
        return;

    SDL_SetTextureBlendMode(target.get(), SDL_BLENDMODE_BLEND);

    {
        // Switch the render target to our texture and draw onto it. The scope
        // restores the previous target on exit — before the blit below, matching
        // the original ordering (blit happens against the restored target).
        detail::SdlRenderTargetScope targetScope(renderer_, target.get());

        // Clear with transparent
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
        SDL_RenderClear(renderer_);

        // User draws on this texture at (0,0) relative coordinates. The target /
        // texture scopes restore SDL state on any exit, including a throw from the
        // callback; isolate it and continue rendering siblings.
        try {
            node->props.draw(renderer_, static_cast<float>(w), static_cast<float>(h));
        } catch (const std::exception& e) {
            reportError("draw", &e);
        } catch (...) {
            reportError("draw", nullptr);
        }
    }

    // Blit the texture to the correct position
    SDL_FRect dst = {x, y, static_cast<float>(w), static_cast<float>(h)};
    SDL_RenderCopyF(renderer_, target.get(), nullptr, &dst);
}

void SdlRenderer::drawRoundedRect(float x, float y, float w, float h, float radius, uint32_t color, bool filled) {
    // Convert 0xRRGGBBAA to individual components
    Uint8 r = (color >> 24) & 0xFF;
    Uint8 g = (color >> 16) & 0xFF;
    Uint8 b = (color >> 8) & 0xFF;
    Uint8 a = color & 0xFF;

    Sint16 x1 = static_cast<Sint16>(x);
    Sint16 y1 = static_cast<Sint16>(y);
    Sint16 x2 = static_cast<Sint16>(x + w);
    Sint16 y2 = static_cast<Sint16>(y + h);
    Sint16 rad = static_cast<Sint16>(std::min(radius, std::min(w, h) / 2));

    if (rad <= 0) {
        // No radius - use simple rect
        if (filled) {
            boxRGBA(renderer_, x1, y1, x2, y2, r, g, b, a);
        } else {
            rectangleRGBA(renderer_, x1, y1, x2, y2, r, g, b, a);
        }
    } else {
        if (filled) {
            roundedBoxRGBA(renderer_, x1, y1, x2, y2, rad, r, g, b, a);
        } else {
            roundedRectangleRGBA(renderer_, x1, y1, x2, y2, rad, r, g, b, a);
        }
    }
}

void SdlRenderer::drawText(const std::string& text, float x, float y, float fontSize, uint32_t color,
                           const std::string& fontName) {
    if (text.empty())
        return;

    int size = static_cast<int>(fontSize + 0.5f);
    TTF_Font* font = getFont(fontName, size);
    if (!font)
        return;

    SDL_Color c = {static_cast<Uint8>((color >> 24) & 0xFF), static_cast<Uint8>((color >> 16) & 0xFF),
                   static_cast<Uint8>((color >> 8) & 0xFF), static_cast<Uint8>(color & 0xFF)};

    // Render at native size - no scaling needed
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), c);
    if (!surface)
        return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return;
    }

    SDL_FRect dst = {x, y, static_cast<float>(surface->w), static_cast<float>(surface->h)};
    SDL_RenderCopyF(renderer_, texture, nullptr, &dst);

    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

bool initSDL() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return false;
    }
    if (TTF_Init() < 0) {
        SDL_Quit();
        return false;
    }
    return true;
}

void quitSDL() {
    TTF_Quit();
    SDL_Quit();
}

}  // namespace sdl
}  // namespace yui
