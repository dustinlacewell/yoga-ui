#pragma once

#include "../core/ErrorHandler.hpp"
#include "../core/Node.hpp"
#include "../render/Backend.hpp"

#include <exception>
#include <string>
#include <string_view>
#include <unordered_map>

// Forward declare NanoVG context
struct NVGcontext;

namespace yui {
namespace nvg {

// NanoVG backend for yui: provides the render::IRenderBackend primitives that
// the backend-neutral walk (render::renderTree) draws with. Also serves as the
// host's text measurer (install via Host::setTextMeasurer — IRenderBackend is
// an ITextMeasurer).
class NvgRenderer : public render::IRenderBackend {
public:
    // The optional ErrorHandler keeps the renderer decoupled from core Host: a
    // throwing Canvas draw callback is caught and routed here (default: swallow)
    // rather than escaping into the draw-time C boundary.
    NvgRenderer(NVGcontext* vg, int fontId = -1, ErrorHandler onError = {});

    // Render a node tree: frame bracket around the neutral walk. noexcept
    // backstop: rendering runs inside the platform's draw callback (a C
    // boundary), so a throw must never escape.
    void render(Node* root) noexcept;

    // ITextMeasurer: measure text using this renderer's nanovg context/font.
    Size measure(const std::string& text, float fontSize, float maxWidth, const std::string& font) const override;
    float measureRun(std::string_view run, float fontSize, std::string_view font) const override;
    FontMetrics fontMetrics(float fontSize, std::string_view font) const override;

    // Register a named font face usable from Text/Input `.font(name)`.
    //   - From a .ttf path: loads it via nvgCreateFont and returns the handle
    //     (or -1 on failure). The name is what consumers reference.
    //   - From an existing nanovg font handle: aliases `name` to a font already
    //     loaded into this context (e.g. the host's UI font), no reload.
    // Per-renderer (hence per-host / per-GL-context): the handles die with the
    // context, exactly when this renderer does. Re-register by name after a
    // context rebuild. Returns the handle.
    int registerFont(const std::string& name, const std::string& path);
    void registerFont(const std::string& name, int existingHandle);

    // render::IRenderBackend primitives (called by render::renderTree).
    void beginFrame() override;
    void endFrame() override;
    void fillRect(const render::Rect& r, uint32_t color, float radius) override;
    void strokeRect(const render::Rect& r, uint32_t color, float radius, float width) override;
    void pushClip(const render::Rect& r, float radius) override;
    void popClip() override;
    void drawTextRun(const std::string& run, float x, float y, float fontSize, uint32_t color,
                     const std::string& font) override;
    void drawCanvas(const CanvasNode& node, const render::Rect& r) override;

private:
    // Begin a path shaped as r (rounded when radius > 0) — shared by fill/stroke.
    void pathRect(const render::Rect& r, float radius);

    // Route a caught draw exception to the optional sink (no-op if unset).
    void reportError(std::string_view where, const std::exception* eOrNull) noexcept;

    // Select the nanovg face for a (possibly empty) font name: a registered name
    // → its handle; empty/unknown → the default (fontId_, else nvg "default").
    // Shared by drawTextRun/measure so draw and measure never diverge.
    void selectFont(const std::string& name) const;

    NVGcontext* vg_;
    int fontId_;
    ErrorHandler onError_;

    // Open pushClip scopes; endFrame unwinds any left behind by an exception
    // the walk's backstop caught mid-frame.
    int clipDepth_ = 0;

    // Named font registry (name → nanovg handle). Resolved by selectFont; the
    // handles are owned by vg_ and die with it. Read-only in measure()/selectFont
    // (const), populated only by registerFont.
    std::unordered_map<std::string, int> fonts_;
};

}  // namespace nvg
}  // namespace yui
