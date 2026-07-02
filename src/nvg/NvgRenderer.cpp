#include <yui/nvg/NvgRenderer.hpp>
#include <yui/nvg/detail/NvgScopes.hpp>
#include <yui/render/TreeRenderer.hpp>

#include <exception>

#include <nanovg.h>

namespace yui {
namespace nvg {

namespace {

NVGcolor toNvgColor(uint32_t c) {
    return nvgRGBA((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

}  // namespace

NvgRenderer::NvgRenderer(NVGcontext* vg, int fontId, ErrorHandler onError)
    : vg_(vg), fontId_(fontId), onError_(std::move(onError)) {}

int NvgRenderer::registerFont(const std::string& name, const std::string& path) {
    if (!vg_)
        return -1;
    int handle = nvgCreateFont(vg_, name.c_str(), path.c_str());
    if (handle >= 0)
        fonts_[name] = handle;
    return handle;
}

void NvgRenderer::registerFont(const std::string& name, int existingHandle) {
    if (existingHandle >= 0)
        fonts_[name] = existingHandle;
}

void NvgRenderer::selectFont(const std::string& name) const {
    // Named face if registered; otherwise the renderer default (fontId_, else
    // nanovg's "default"). Keeps draw and measure on the SAME face for a node.
    if (!name.empty()) {
        auto it = fonts_.find(name);
        if (it != fonts_.end()) {
            nvgFontFaceId(vg_, it->second);
            return;
        }
    }
    if (fontId_ >= 0) {
        nvgFontFaceId(vg_, fontId_);
    } else {
        nvgFontFace(vg_, "default");
    }
}

void NvgRenderer::reportError(std::string_view where, const std::exception* eOrNull) noexcept {
    if (!onError_)
        return;
    try {
        onError_(where, eOrNull);
    } catch (...) {}
}

float NvgRenderer::measureRun(std::string_view run, float fontSize, std::string_view font) const {
    if (!vg_) {
        return fallbackMeasureRun(run, fontSize, font);
    }

    nvgFontSize(vg_, fontSize);
    selectFont(std::string(font));
    // Explicit align: measurement must never inherit whatever align the last
    // draw left on the shared context. The return value is the advance
    // (align-independent), never the string's ink bounds.
    nvgTextAlign(vg_, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    return nvgTextBounds(vg_, 0, 0, run.data(), run.data() + run.size(), nullptr);
}

FontMetrics NvgRenderer::fontMetrics(float fontSize, std::string_view font) const {
    if (!vg_) {
        return fallbackFontMetrics(fontSize, font);
    }

    nvgFontSize(vg_, fontSize);
    selectFont(std::string(font));
    float ascender = 0, descender = 0, lineHeight = 0;
    nvgTextMetrics(vg_, &ascender, &descender, &lineHeight);
    // nanovg reports the descender negative (below baseline); FontMetrics
    // carries it positive.
    return {ascender, -descender, lineHeight};
}

void NvgRenderer::render(Node* root) noexcept {
    if (!vg_ || !root)
        return;

    beginFrame();
    render::renderTree(root, *this, onError_);
    endFrame();
}

void NvgRenderer::beginFrame() {
    // One frame-wide save: endFrame's matching restore hands the context back
    // to the embedder in the state we received it, whatever the primitives
    // touched in between (font, fill color, text align, scissor).
    nvgSave(vg_);
    clipDepth_ = 0;
}

void NvgRenderer::endFrame() {
    for (; clipDepth_ > 0; --clipDepth_) {
        nvgRestore(vg_);
    }
    nvgRestore(vg_);
}

void NvgRenderer::fillRect(const render::Rect& r, uint32_t color, float radius) {
    nvgBeginPath(vg_);
    pathRect(r, radius);
    nvgFillColor(vg_, toNvgColor(color));
    nvgFill(vg_);
}

void NvgRenderer::strokeRect(const render::Rect& r, uint32_t color, float radius, float width) {
    nvgBeginPath(vg_);
    pathRect(r, radius);
    nvgStrokeColor(vg_, toNvgColor(color));
    nvgStrokeWidth(vg_, width);
    nvgStroke(vg_);
}

void NvgRenderer::pathRect(const render::Rect& r, float radius) {
    if (radius > 0) {
        nvgRoundedRect(vg_, r.x, r.y, r.w, r.h, radius);
    } else {
        nvgRect(vg_, r.x, r.y, r.w, r.h);
    }
}

void NvgRenderer::pushClip(const render::Rect& r, float /*radius*/) {
    // Scissor in ABSOLUTE coordinates (no translate): nvgIntersectScissor nests
    // with any enclosing clip, and the paired save makes popClip's restore
    // exact. Rect-only today; the radius is advisory.
    nvgSave(vg_);
    nvgIntersectScissor(vg_, r.x, r.y, r.w, r.h);
    ++clipDepth_;
}

void NvgRenderer::popClip() {
    if (clipDepth_ <= 0)
        return;
    nvgRestore(vg_);
    --clipDepth_;
}

void NvgRenderer::drawTextRun(const std::string& run, float x, float y, float fontSize, uint32_t color,
                              const std::string& font) {
    nvgFontSize(vg_, fontSize);
    selectFont(font);
    nvgFillColor(vg_, toNvgColor(color));
    nvgTextAlign(vg_, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgText(vg_, x, y, run.c_str(), nullptr);
}

void NvgRenderer::drawCanvas(const CanvasNode& node, const render::Rect& r) {
    // The save scope restores NanoVG state on any exit, including a throw from the
    // user draw callback. Isolate that throw and continue rendering siblings.
    detail::NvgSaveScope saveScope(vg_);
    nvgTranslate(vg_, r.x, r.y);
    try {
        node.props.draw(vg_, r.w, r.h);
    } catch (const std::exception& e) {
        reportError("draw", &e);
    } catch (...) {
        reportError("draw", nullptr);
    }
}

}  // namespace nvg
}  // namespace yui
