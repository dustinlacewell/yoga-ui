#pragma once

#include <utility>

#include <SDL.h>

namespace yui {
namespace sdl {
namespace detail {

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

    SdlTexture(SdlTexture&& other) noexcept : texture_(std::exchange(other.texture_, nullptr)) {}
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
