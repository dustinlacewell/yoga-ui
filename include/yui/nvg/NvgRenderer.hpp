#pragma once

#include "../core/Node.hpp"

// Forward declare NanoVG context
struct NVGcontext;

namespace yui {
namespace nvg {

// NanoVG renderer for yui. Also serves as the host's text measurer
// (install via Host::setTextMeasurer).
class NvgRenderer : public ITextMeasurer {
public:
    NvgRenderer(NVGcontext* vg, int fontId = -1);

    // Render a node tree
    void render(Node* root);

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

    NVGcontext* vg_;
    int fontId_;
};

}  // namespace nvg
}  // namespace yui
