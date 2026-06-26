#pragma once

#include "../core/ErrorHandler.hpp"
#include "../core/Node.hpp"

#include <exception>
#include <string>
#include <string_view>
#include <unordered_map>

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
    Size measure(const std::string& text, float fontSize, float maxWidth,
                 const std::string& font) const override;

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

    // Select the nanovg face for a (possibly empty) font name: a registered name
    // → its handle; empty/unknown → the default (fontId_, else nvg "default").
    // Shared by drawText/drawInput/measure so draw and measure never diverge.
    void selectFont(const std::string& name) const;

    NVGcontext* vg_;
    int fontId_;
    ErrorHandler onError_;

    // Named font registry (name → nanovg handle). Resolved by selectFont; the
    // handles are owned by vg_ and die with it. Read-only in measure()/selectFont
    // (const), populated only by registerFont.
    std::unordered_map<std::string, int> fonts_;
};

}  // namespace nvg
}  // namespace yui
