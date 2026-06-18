#include "yui/sdl/SdlRenderer.hpp"

#include "yui/sdl/detail/SdlScopes.hpp"

#include <algorithm>
#include <cmath>

#include <SDL2_gfxPrimitives.h>

namespace yui {
namespace sdl {

SdlRenderer::SdlRenderer(SDL_Renderer* renderer, const std::string& fontPath, int baseFontSize)
    : renderer_(renderer), fontPath_(fontPath), baseFontSize_(baseFontSize) {
    // Pre-load base font
    getFont(baseFontSize);
}

SdlRenderer::~SdlRenderer() {
    // Clean up font cache
    for (auto& [size, font] : fontCache_) {
        if (font)
            TTF_CloseFont(font);
    }
    fontCache_.clear();
}

TTF_Font* SdlRenderer::getFont(int size) const {
    if (size < 1)
        size = 1;

    auto it = fontCache_.find(size);
    if (it != fontCache_.end())
        return it->second;

    TTF_Font* font = TTF_OpenFont(fontPath_.c_str(), size);
    if (font) {
        fontCache_[size] = font;
    }
    return font;
}

Size SdlRenderer::measure(const std::string& text, float fontSize, float maxWidth) const {
    if (text.empty())
        return {0, fontSize};

    int size = static_cast<int>(fontSize + 0.5f);
    TTF_Font* font = getFont(size);
    if (!font)
        return {0, fontSize};

    int w = 0, h = 0;
    TTF_SizeUTF8(font, text.c_str(), &w, &h);

    float width = static_cast<float>(w);
    float height = static_cast<float>(h);

    if (maxWidth > 0 && width > maxWidth) {
        int lines = static_cast<int>(std::ceil(width / maxWidth));
        width = maxWidth;
        height = height * lines;
    }

    return {width, height};
}

void SdlRenderer::render(Node* root) {
    if (!root)
        return;
    renderNode(root, 0, 0);
}

void SdlRenderer::renderNode(Node* node, float offsetX, float offsetY) {
    float x = offsetX + node->layout.left;
    float y = offsetY + node->layout.top;

    switch (node->type()) {
    case PrimitiveType::Box:
        renderBox(static_cast<BoxNode*>(node), x, y);
        break;
    case PrimitiveType::Text:
        renderText(static_cast<TextNode*>(node), x, y);
        break;
    case PrimitiveType::Input:
        renderInput(static_cast<InputNode*>(node), x, y);
        break;
    case PrimitiveType::Scroll:
        renderScroll(static_cast<ScrollNode*>(node), offsetX, offsetY);
        return;  // renderScroll handles its own children
    case PrimitiveType::Canvas:
        renderCanvas(static_cast<CanvasNode*>(node), x, y);
        break;
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
    float fontSize = p.fontSize.value_or(14.0f);
    uint32_t color = p.color.value_or(0xFFFFFFFF);

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

    drawText(node->props.text, x, y, fontSize, color);
}

void SdlRenderer::renderInput(InputNode* node, float x, float y) {
    float w = node->layout.width;
    float h = node->layout.height;
    auto& p = node->props;

    // Default styles for Input
    constexpr uint32_t DEFAULT_BG = 0x333333FF;
    constexpr uint32_t DEFAULT_BORDER = 0x666666FF;
    constexpr uint32_t DEFAULT_HOVER_BORDER = 0x888888FF;
    constexpr uint32_t DEFAULT_FOCUS_BORDER = 0x4a9fffFF;

    // Resolve styles: base < hover < focus (with defaults for Input)
    uint32_t bg = p.backgroundColor.value_or(DEFAULT_BG);
    uint32_t border = p.borderColor.value_or(DEFAULT_BORDER);
    float borderW = p.borderWidth.value_or(1.0f);
    float radius = p.borderRadius.value_or(4.0f);
    float fontSize = p.fontSize.value_or(14.0f);
    uint32_t color = p.color.value_or(0xFFFFFFFF);

    if (node->hovered) {
        // Apply default hover style, then user overrides
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
                color = *p.hoverStyle->color;
        }
    }
    if (node->focused) {
        // Apply default focus style, then user overrides
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
                color = *p.focusStyle->color;
        }
    }

    drawRoundedRect(x, y, w, h, radius, bg, true);
    if (borderW > 0) {
        drawRoundedRect(x, y, w, h, radius, border, false);
    }

    float padding = 8;

    std::string displayText;
    if (!p.value.empty()) {
        if (p.password.value_or(false)) {
            displayText = std::string(p.value.size(), '*');
        } else {
            displayText = p.value;
        }
        drawText(displayText, x + padding, y + (h - fontSize) / 2, fontSize, color);
    } else if (p.placeholder) {
        drawText(*p.placeholder, x + padding, y + (h - fontSize) / 2, fontSize, 0x888888FF);
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

        // User draws on this texture at (0,0) relative coordinates
        node->props.draw(renderer_, static_cast<float>(w), static_cast<float>(h));
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

void SdlRenderer::drawText(const std::string& text, float x, float y, float fontSize, uint32_t color) {
    if (text.empty())
        return;

    int size = static_cast<int>(fontSize + 0.5f);
    TTF_Font* font = getFont(size);
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
