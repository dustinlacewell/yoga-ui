#pragma once

#include "../core/ErrorHandler.hpp"
#include "../core/Node.hpp"

#include <exception>
#include <string>
#include <string_view>

// Forward declare NanoVG context
struct NVGcontext;

namespace yui {
namespace nvg {

// NanoVG renderer for yui. Also serves as the host's text measurer
// (install via Host::setTextMeasurer).
class NvgRenderer : public ITextMeasurer {
public:
    // The optional ErrorHandler keeps the renderer decoupled from core Host: a
    // throwing Canvas draw callback is caught and routed here (default: swallow)
    // rather than escaping into the draw-time C boundary.
    NvgRenderer(NVGcontext* vg, int fontId = -1, ErrorHandler onError = {});

    // Render a node tree. noexcept backstop: rendering runs inside the platform's
    // draw callback (a C boundary), so a throw must never escape.
    void render(Node* root) noexcept;

    // ITextMeasurer: measure text using this renderer's nanovg context/font.
    Size measure(const std::string& text, float fontSize, float maxWidth) const override;

private:
    struct DrawContext {
        float offsetX = 0;
        float offsetY = 0;
    };

    void drawNode(DrawContext& ctx, Node* node);
    void drawBox(DrawContext& ctx, BoxNode* node);
    void drawText(DrawContext& ctx, TextNode* node);
    void drawInput(DrawContext& ctx, InputNode* node);
    void drawScroll(DrawContext& ctx, ScrollNode* node);
    void drawCanvas(DrawContext& ctx, CanvasNode* node);

    // Route a caught draw exception to the optional sink (no-op if unset).
    void reportError(std::string_view where, const std::exception* eOrNull) noexcept;

    NVGcontext* vg_;
    int fontId_;
    ErrorHandler onError_;
};

}  // namespace nvg
}  // namespace yui
