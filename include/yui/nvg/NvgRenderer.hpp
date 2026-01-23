#pragma once

#include "../core/Node.hpp"

// Forward declare NanoVG context
struct NVGcontext;

namespace yui {
namespace nvg {

// NanoVG renderer for yui
class NvgRenderer {
public:
    NvgRenderer(NVGcontext* vg, int fontId = -1);

    // Render a node tree
    void render(Node* root);

    // Register text measure function with yui::Measure
    void registerMeasureFunc();

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
