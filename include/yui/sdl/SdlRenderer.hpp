#pragma once

#include "../core/ErrorHandler.hpp"
#include "../core/Node.hpp"
#include "../detail/TransparentStringHash.hpp"
#include "../render/Backend.hpp"

#include <cstdint>
#include <exception>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <SDL.h>
#include <SDL_ttf.h>

namespace yui {
namespace sdl {

// SDL2 + SDL2_ttf + SDL2_gfx backend for yui: provides the
// render::IRenderBackend primitives that the backend-neutral walk
// (render::renderTree) draws with. Also serves as the host's text measurer
// (install via Host::setTextMeasurer — IRenderBackend is an ITextMeasurer).
class SdlRenderer : public render::IRenderBackend {
public:
    // The optional ErrorHandler keeps the renderer decoupled from core Host: a
    // throwing Canvas draw callback is caught and routed here (default: swallow)
    // rather than escaping into the draw-time C boundary.
    SdlRenderer(SDL_Renderer* renderer, const std::string& fontPath, int baseFontSize = 16, ErrorHandler onError = {});
    ~SdlRenderer() override;

    // Render a node tree: frame bracket around the neutral walk. noexcept
    // backstop: rendering runs inside the platform's draw callback (a C
    // boundary), so a throw must never escape.
    //
    // Embedder contract: after render() returns, the SDL_Renderer's draw color,
    // blend mode, clip rect, and render target are exactly as they were on
    // entry (color/blend captured in beginFrame and restored in endFrame; clip
    // by the clip-stack unwind; target by drawCanvas' RAII scope).
    void render(Node* root) noexcept;

    // ITextMeasurer primitives, backed by this renderer's font cache. `font`
    // selects a registered named face (empty/unknown ⇒ the default font this
    // renderer was constructed with). Sizing (measure) is the shared base
    // implementation over these.
    float measureRun(std::string_view run, float fontSize, std::string_view font) const override;
    FontMetrics fontMetrics(float fontSize, std::string_view font) const override;

    // Register a named font face (a .ttf path) usable from Text/Input `.font(name)`.
    // Fonts open lazily per size on first use. The default font supplied at
    // construction is registered under the empty name "".
    void registerFont(const std::string& name, const std::string& path);

    // render::IRenderBackend primitives (called by render::renderTree).
    void beginFrame() override;
    void endFrame() override;
    void fillRect(const render::Rect& r, uint32_t color, float radius) override;
    // SDL2_gfx outlines are always hairline; the stroke width is advisory.
    void strokeRect(const render::Rect& r, uint32_t color, float radius, float width) override;
    void pushClip(const render::Rect& r, float radius) override;
    void popClip() override;
    void drawTextRun(const std::string& run, float x, float y, float fontSize, uint32_t color,
                     std::string_view font) override;
    void drawCanvas(const CanvasNode& node, const render::Rect& r) override;

private:
    // The clip state as it was before a pushClip, so popClip restores it —
    // previously enabled (restore that rect) or disabled (restore to no clip).
    struct ClipEntry {
        bool hadClip = false;
        SDL_Rect rect{};
    };

    // Draw a rounded rectangle (uses SDL2_gfx)
    void drawRoundedRect(float x, float y, float w, float h, float radius, uint32_t color, bool filled);

    // Get or create the TTF_Font for (font name, size), lazily filling the cache.
    // An empty/unknown name resolves to the default font.
    TTF_Font* getFont(std::string_view font, int size) const;

    // Route a caught draw exception to the optional sink (no-op if unset).
    void reportError(std::string_view where, const std::exception* eOrNull) noexcept;

    SDL_Renderer* renderer_;
    int baseFontSize_;
    ErrorHandler onError_;

    // Saved pre-push clip states; endFrame unwinds any left behind by an
    // exception the walk's backstop caught mid-frame.
    std::vector<ClipEntry> clipStack_;

    // The embedder's draw state, captured by beginFrame and put back by
    // endFrame. Color and blend mode are the renderer state this backend's own
    // primitives mutate (SDL2_gfx sets both per call; the fill path sets both);
    // the clip rect is restored by the clip-stack unwind and the render target
    // by drawCanvas' RAII scope.
    struct SavedDrawState {
        Uint8 r = 0, g = 0, b = 0, a = 0;
        SDL_BlendMode blend = SDL_BLENDMODE_NONE;
    };
    SavedDrawState preFrame_;

    // A registered named face: its .ttf path plus a lazily-filled per-size cache
    // of opened fonts. The default font (construction-time fontPath) lives under
    // the empty name "".
    struct FontFace {
        std::string path;
        // Mutable: lazily populated by getFont, including from the const
        // measureRun/fontMetrics paths.
        mutable std::unordered_map<int, TTF_Font*> sizes;
    };
    // Transparent hash/equality so the per-frame getFont(string_view) lookup
    // never allocates a key.
    std::unordered_map<std::string, FontFace, yui::detail::TransparentStringHash, std::equal_to<>> fonts_;
};

// Initialize SDL2 and SDL2_ttf (call once at startup)
bool initSDL();

// Cleanup SDL2 and SDL2_ttf (call at shutdown)
void quitSDL();

}  // namespace sdl
}  // namespace yui
