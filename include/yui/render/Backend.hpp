#pragma once

#include "../core/Measure.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace yui {
class CanvasNode;
}

namespace yui::render {

// Axis-aligned rectangle in absolute (root-relative) coordinates. The neutral
// tree walk resolves every node to absolute space before calling a backend, so
// backends never track offsets or translations of their own.
struct Rect {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
};

// The primitive surface a backend provides to the backend-neutral tree walk
// (render::renderTree). Backends implement these plus the ITextMeasurer
// primitives (measureRun/fontMetrics); everything above the primitives — the
// node switch, style cascade, text wrapping, scroll clip/offset math, input
// chrome, caret — lives in the neutral layer and is identical across backends
// by construction.
class IRenderBackend : public ITextMeasurer {
public:
    // Frame bracket around one renderTree walk. endFrame() restores every
    // embedder-observable backend state the frame's primitives touched,
    // including a clip stack left unbalanced when the walk's exception backstop
    // fired mid-frame — so an embedder can interleave its own drawing around a
    // render() without re-asserting state. What that covers is backend-specific
    // and documented at each renderer's render() (NVG: the nanovg state stack,
    // no direct GL calls; SDL: draw color, blend mode, clip rect, target).
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;

    // color is 0xRRGGBBAA (the Props convention). radius <= 0 => square corners.
    virtual void fillRect(const Rect& r, uint32_t color, float radius) = 0;
    // width is advisory where the backend cannot honor it (SDL2_gfx outlines
    // are always hairline).
    virtual void strokeRect(const Rect& r, uint32_t color, float radius, float width) = 0;

    // Clip stack: intersects the current clip; pushes and pops must nest.
    // radius is ADVISORY: neither shipped backend can clip a rounded region
    // (nanovg scissors are axis-aligned rects, SDL clip rects are integer
    // rects), so content may bleed into the corner arcs of a rounded clip. A
    // backend with stencil/mask clipping could honor it exactly.
    virtual void pushClip(const Rect& r, float radius) = 0;
    virtual void popClip() = 0;

    // One pre-wrapped run, top-left anchored at (x, y). `font` names a
    // registered face (empty => the backend default), the same contract as
    // ITextMeasurer::measure — so a run draws in the face it was measured in.
    // The run stays std::string because SDL_ttf requires a null-terminated
    // UTF-8 string (and the walk materializes each run anyway); the font is a
    // view, resolved against the backend's registry without allocating.
    virtual void drawTextRun(const std::string& run, float x, float y, float fontSize, uint32_t color,
                             std::string_view font) = 0;

    // Hand the node's opaque draw context to the user callback with the origin
    // translated to (r.x, r.y). Must isolate a throwing callback: report it,
    // restore backend state, and let the walk continue with siblings.
    virtual void drawCanvas(const CanvasNode& node, const Rect& r) = 0;
};

}  // namespace yui::render
