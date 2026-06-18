#pragma once

#include <SDL.h>

#include <utility>

namespace yui {
namespace sdl {
namespace detail {

// RAII clip-rect save/restore. Captures the renderer's current clip rect on
// construction and restores it on destruction — whether it was previously
// enabled (restore to the saved rect) or disabled (restore to no clip). The
// caller applies its own clip after constructing this scope; the restore happens
// on any scope exit. Mirrors the SDL_RenderGetClipRect / SDL_RenderSetClipRect
// pairing it replaces.
class SdlClipScope {
public:
    explicit SdlClipScope(SDL_Renderer* renderer) : renderer_(renderer) {
        hadClip_ = SDL_RenderIsClipEnabled(renderer_);
        if (hadClip_) {
            SDL_RenderGetClipRect(renderer_, &prevClip_);
        }
    }
    ~SdlClipScope() {
        if (hadClip_) {
            SDL_RenderSetClipRect(renderer_, &prevClip_);
        } else {
            SDL_RenderSetClipRect(renderer_, nullptr);
        }
    }

    // The saved clip rect, valid only when the renderer had a clip on entry.
    SDL_bool hadClip() const { return hadClip_; }
    const SDL_Rect& prevClip() const { return prevClip_; }

    SdlClipScope(const SdlClipScope&) = delete;
    SdlClipScope& operator=(const SdlClipScope&) = delete;

private:
    SDL_Renderer* renderer_;
    SDL_Rect prevClip_{};
    SDL_bool hadClip_ = SDL_FALSE;
};

// RAII render-target save/restore. Captures the current render target on
// construction, switches to the supplied target, and restores the captured
// target on destruction — on any scope exit. Replaces the manual
// SDL_GetRenderTarget / SDL_SetRenderTarget pairing.
class SdlRenderTargetScope {
public:
    SdlRenderTargetScope(SDL_Renderer* renderer, SDL_Texture* target)
        : renderer_(renderer), prevTarget_(SDL_GetRenderTarget(renderer)) {
        SDL_SetRenderTarget(renderer_, target);
    }
    ~SdlRenderTargetScope() { SDL_SetRenderTarget(renderer_, prevTarget_); }

    SdlRenderTargetScope(const SdlRenderTargetScope&) = delete;
    SdlRenderTargetScope& operator=(const SdlRenderTargetScope&) = delete;

private:
    SDL_Renderer* renderer_;
    SDL_Texture* prevTarget_;
};

// Move-only RAII owner for an SDL_Texture, destroyed via SDL_DestroyTexture on
// scope exit. Replaces manual SDL_CreateTexture / SDL_DestroyTexture pairs so the
// texture is freed on any early return or exception.
class SdlTexture {
public:
    SdlTexture() = default;
    explicit SdlTexture(SDL_Texture* texture) : texture_(texture) {}
    ~SdlTexture() { reset(); }

    SdlTexture(SdlTexture&& other) noexcept
        : texture_(std::exchange(other.texture_, nullptr)) {}
    SdlTexture& operator=(SdlTexture&& other) noexcept {
        if (this != &other) {
            reset();
            texture_ = std::exchange(other.texture_, nullptr);
        }
        return *this;
    }

    SdlTexture(const SdlTexture&) = delete;
    SdlTexture& operator=(const SdlTexture&) = delete;

    SDL_Texture* get() const { return texture_; }
    explicit operator bool() const { return texture_ != nullptr; }

    void reset() {
        if (texture_) {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }
    }

private:
    SDL_Texture* texture_ = nullptr;
};

}  // namespace detail
}  // namespace sdl
}  // namespace yui
