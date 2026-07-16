#pragma once

#include "../core/ErrorHandler.hpp"
#include "../core/Node.hpp"
#include "../detail/TransparentStringHash.hpp"
#include "../render/Backend.hpp"

#include <exception>
#include <functional>
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
    //
    // Embedder contract: call between your nvgBeginFrame/nvgEndFrame. After
    // render() returns, the nanovg context state (transform, scissor, paints,
    // font, text align) is exactly as it was on entry — beginFrame/endFrame
    // bracket the walk in one frame-wide nvgSave/nvgRestore. yui issues no GL
    // calls of its own: GL state changes only inside nanovg's deferred flush,
    // at the embedder's own nvgEndFrame.
    void render(Node* root) noexcept;

    // ITextMeasurer primitives, backed by this renderer's nanovg context/font.
    // Sizing (measure) is the shared base implementation over these.
    float measureRun(std::string_view run, float fontSize, std::string_view font) const override;
    FontMetrics fontMetrics(float fontSize, std::string_view font) const override;

    // The transform scale text will be PAINTED under, so measurement rasters
    // glyphs at the same pixel size painting will. A hinting font backend
    // (Rack bundles a FreeType fontstash) rounds each glyph's advance at the
    // rasterized size — advances are NOT linear across sizes — so a host
    // drawn at a fixed magnification (e.g. 2x chrome) paints text wider or
    // narrower than a 1x measurement, and text laid out flush against a clip
    // edge gets its tail cut. Set the host's paint scale here (default 1);
    // measured values stay in layout units. Changing it changes measurement:
    // re-layout (host markDirty) after a change.
    void setRenderScale(float scale) { renderScale_ = scale > 0 ? scale : 1.0f; }
    float renderScale() const { return renderScale_; }

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
                     std::string_view font) override;
    void drawCanvas(const CanvasNode& node, const render::Rect& r) override;

private:
    // Begin a path shaped as r (rounded when radius > 0) — shared by fill/stroke.
    void pathRect(const render::Rect& r, float radius);

    // Route a caught draw exception to the optional sink (no-op if unset).
    void reportError(std::string_view where, const std::exception* eOrNull) noexcept;

    // Select the nanovg face for a (possibly empty) font name: a registered name
    // → its handle; empty/unknown → the default (fontId_, else nvg "default").
    // Shared by drawTextRun/measure so draw and measure never diverge.
    void selectFont(std::string_view name) const;

    NVGcontext* vg_;
    int fontId_;
    ErrorHandler onError_;
    float renderScale_ = 1.0f;  // see setRenderScale

    // Open pushClip scopes; endFrame unwinds any left behind by an exception
    // the walk's backstop caught mid-frame.
    int clipDepth_ = 0;

    // Named font registry (name → nanovg handle). Resolved by selectFont; the
    // handles are owned by vg_ and die with it. Read-only in measure()/selectFont
    // (const), populated only by registerFont. Transparent hash/equality so the
    // per-frame selectFont(string_view) lookup never allocates a key.
    std::unordered_map<std::string, int, yui::detail::TransparentStringHash, std::equal_to<>> fonts_;
};

}  // namespace nvg
}  // namespace yui
