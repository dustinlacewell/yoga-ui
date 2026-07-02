#include "yui/sdl/SdlRenderer.hpp"

#include "yui/render/TreeRenderer.hpp"
#include "yui/sdl/detail/SdlScopes.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

#include <SDL2_gfxPrimitives.h>

namespace yui {
namespace sdl {

namespace {

SDL_Rect intersect(const SDL_Rect& a, const SDL_Rect& b) {
    int x1 = std::max(a.x, b.x);
    int y1 = std::max(a.y, b.y);
    int x2 = std::min(a.x + a.w, b.x + b.w);
    int y2 = std::min(a.y + a.h, b.y + b.h);
    return {x1, y1, std::max(0, x2 - x1), std::max(0, y2 - y1)};
}

}  // namespace

SdlRenderer::SdlRenderer(SDL_Renderer* renderer, const std::string& fontPath, int baseFontSize, ErrorHandler onError)
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
    } catch (...) {}
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

Size SdlRenderer::measure(const std::string& text, float fontSize, float maxWidth, const std::string& font) const {
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

    beginFrame();
    render::renderTree(root, *this, onError_);
    endFrame();
}

void SdlRenderer::beginFrame() {
    clipStack_.clear();
}

void SdlRenderer::endFrame() {
    // Unwind clip scopes left open when the walk's backstop caught mid-frame,
    // handing the renderer back with its pre-frame clip state.
    while (!clipStack_.empty()) {
        popClip();
    }
}

void SdlRenderer::fillRect(const render::Rect& r, uint32_t color, float radius) {
    drawRoundedRect(r.x, r.y, r.w, r.h, radius, color, true);
}

void SdlRenderer::strokeRect(const render::Rect& r, uint32_t color, float radius,
                             float /*width — SDL2_gfx outlines are hairline*/) {
    drawRoundedRect(r.x, r.y, r.w, r.h, radius, color, false);
}

void SdlRenderer::pushClip(const render::Rect& r, float /*radius — rect-only clip*/) {
    ClipEntry prev;
    prev.hadClip = SDL_RenderIsClipEnabled(renderer_) == SDL_TRUE;
    if (prev.hadClip) {
        SDL_RenderGetClipRect(renderer_, &prev.rect);
    }

    SDL_Rect clip = {static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.w), static_cast<int>(r.h)};
    if (prev.hadClip) {
        clip = intersect(clip, prev.rect);
    }
    SDL_RenderSetClipRect(renderer_, &clip);
    clipStack_.push_back(prev);
}

void SdlRenderer::popClip() {
    if (clipStack_.empty())
        return;
    ClipEntry prev = clipStack_.back();
    clipStack_.pop_back();
    SDL_RenderSetClipRect(renderer_, prev.hadClip ? &prev.rect : nullptr);
}

void SdlRenderer::drawTextRun(const std::string& run, float x, float y, float fontSize, uint32_t color,
                              const std::string& font) {
    if (run.empty())
        return;

    int size = static_cast<int>(fontSize + 0.5f);
    TTF_Font* fontPtr = getFont(font, size);
    if (!fontPtr)
        return;

    SDL_Color c = {static_cast<Uint8>((color >> 24) & 0xFF), static_cast<Uint8>((color >> 16) & 0xFF),
                   static_cast<Uint8>((color >> 8) & 0xFF), static_cast<Uint8>(color & 0xFF)};

    // Render at native size - no scaling needed
    SDL_Surface* surface = TTF_RenderUTF8_Blended(fontPtr, run.c_str(), c);
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

void SdlRenderer::drawCanvas(const CanvasNode& node, const render::Rect& r) {
    int w = static_cast<int>(r.w);
    int h = static_cast<int>(r.h);
    if (w <= 0 || h <= 0)
        return;

    // Create a texture as render target for the canvas. The RAII holder destroys
    // it on any scope exit (early return, exception, or normal fall-through).
    detail::SdlTexture target(SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h));
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
            node.props.draw(renderer_, static_cast<float>(w), static_cast<float>(h));
        } catch (const std::exception& e) {
            reportError("draw", &e);
        } catch (...) {
            reportError("draw", nullptr);
        }
    }

    // Blit the texture to the correct position
    SDL_FRect dst = {r.x, r.y, static_cast<float>(w), static_cast<float>(h)};
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
