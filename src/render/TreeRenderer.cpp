#include <yui/core/Node.hpp>
#include <yui/core/RenderDefaults.hpp>
#include <yui/render/StyleResolver.hpp>
#include <yui/render/TreeRenderer.hpp>

#include <chrono>
#include <exception>
#include <string>

namespace yui::render {
namespace {

// Route a caught exception to the optional sink. The sink itself must not
// throw back into an unwinding frame.
void report(const ErrorHandler& onError, std::string_view where, const std::exception* eOrNull) noexcept {
    if (!onError)
        return;
    try {
        onError(where, eOrNull);
    } catch (...) {}
}

// Wall-clock caret blink phase: (ms % period) < onMs => visible. Same cadence
// both backends used before extraction.
bool caretBlinkOn() {
    namespace rd = render_defaults;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                  .count();
    return (ms % rd::kCaretBlinkPeriodMs) < rd::kCaretBlinkOnMs;
}

// The run an Input displays and its color: the (possibly password-masked)
// displayText, else the placeholder in the placeholder color.
struct InputRun {
    std::string text;
    uint32_t color = 0;
};

InputRun inputDisplayRun(const InputNode& node, uint32_t textColor) {
    const auto& p = node.props;
    if (!node.displayText.empty()) {
        if (p.password.value_or(false))
            return {std::string(node.displayText.length(), '*'), textColor};
        return {node.displayText, textColor};
    }
    if (p.placeholder)
        return {*p.placeholder, render_defaults::kPlaceholderColor};
    return {std::string{}, textColor};
}

class TreeWalker {
public:
    TreeWalker(IRenderBackend& backend, const ErrorHandler& onError) : backend_(backend), onError_(onError) {}

    void drawNode(const Node* node, float offsetX, float offsetY);

private:
    void drawBox(const BoxNode* node, const Rect& r);
    void drawText(const TextNode* node, float x, float y);
    void drawInput(const InputNode* node, const Rect& r);
    void drawScroll(const ScrollNode* node, const Rect& r);
    void drawCanvas(const CanvasNode* node, const Rect& r);

    void paintBoxChrome(const ResolvedBoxStyle& s, const Rect& r);
    void drawCaret(const InputNode& node, const InputRun& run, const Rect& r, const ResolvedInputStyle& s,
                   const std::string& font);

    IRenderBackend& backend_;
    const ErrorHandler& onError_;
};

void TreeWalker::drawNode(const Node* node, float offsetX, float offsetY) {
    float x = offsetX + node->layout.left;
    float y = offsetY + node->layout.top;
    Rect r{x, y, node->layout.width, node->layout.height};

    // No default: every enumerator is cased, so -Wswitch flags a newly-added
    // PrimitiveType that has no draw path (which would otherwise silently render
    // nothing). The `handled` sentinel lets us report the only remaining failure
    // mode — a corrupt/out-of-range type value — AFTER the switch, without a
    // default that would suppress -Wswitch.
    bool handled = false;
    switch (node->type()) {
    case PrimitiveType::Box:
        drawBox(static_cast<const BoxNode*>(node), r);
        handled = true;
        break;
    case PrimitiveType::Text:
        drawText(static_cast<const TextNode*>(node), x, y);
        handled = true;
        break;
    case PrimitiveType::Input:
        drawInput(static_cast<const InputNode*>(node), r);
        handled = true;
        break;
    case PrimitiveType::Scroll:
        drawScroll(static_cast<const ScrollNode*>(node), r);
        return;  // drawScroll handles its own children
    case PrimitiveType::Canvas:
        drawCanvas(static_cast<const CanvasNode*>(node), r);
        handled = true;
        break;
    }
    if (!handled) {  // only reachable for a corrupt type value
        report(onError_, "renderTree: unknown PrimitiveType", nullptr);
        return;
    }

    for (auto& child : node->children) {
        drawNode(child.get(), x, y);
    }
}

void TreeWalker::paintBoxChrome(const ResolvedBoxStyle& s, const Rect& r) {
    if (s.backgroundColor)
        backend_.fillRect(r, *s.backgroundColor, s.borderRadius);
    if (s.borderColor && s.borderWidth > 0)
        backend_.strokeRect(r, *s.borderColor, s.borderRadius, s.borderWidth);
}

void TreeWalker::drawBox(const BoxNode* node, const Rect& r) {
    paintBoxChrome(resolveBox(node->props, node->hovered, node->focused), r);
}

void TreeWalker::drawText(const TextNode* node, float x, float y) {
    const auto& p = node->props;
    if (p.text.empty())
        return;
    auto s = resolveText(p, node->hovered, node->focused);
    backend_.drawTextRun(p.text, x, y, s.fontSize, s.color, p.font.value_or(std::string{}));
}

void TreeWalker::drawScroll(const ScrollNode* node, const Rect& r) {
    auto s = resolveBox(node->props, node->hovered, node->focused);
    paintBoxChrome(s, r);

    backend_.pushClip(r, s.borderRadius);
    float childOffsetX = r.x - node->scrollOffsetX;
    float childOffsetY = r.y - node->scrollOffsetY;
    for (auto& child : node->children) {
        drawNode(child.get(), childOffsetX, childOffsetY);
    }
    backend_.popClip();
}

void TreeWalker::drawInput(const InputNode* node, const Rect& r) {
    namespace rd = render_defaults;
    auto s = resolveInput(node->props, node->hovered, node->focused);

    backend_.fillRect(r, s.backgroundColor, s.borderRadius);
    if (s.borderWidth > 0)
        backend_.strokeRect(r, s.borderColor, s.borderRadius, s.borderWidth);

    const std::string font = node->props.font.value_or(std::string{});
    InputRun run = inputDisplayRun(*node, s.color);
    if (!run.text.empty()) {
        backend_.drawTextRun(run.text, r.x + rd::kInputTextPad, r.y + (r.h - s.fontSize) / 2, s.fontSize, run.color,
                             font);
    }

    if (node->focused && caretBlinkOn())
        drawCaret(*node, run, r, s, font);
}

void TreeWalker::drawCaret(const InputNode& node, const InputRun& run, const Rect& r, const ResolvedInputStyle& s,
                           const std::string& font) {
    namespace rd = render_defaults;
    float caretX = r.x + rd::kInputTextPad;
    // Advance past the rendered text; the run is the masked display string, so
    // the caret tracks password dots. A placeholder never advances the caret
    // (displayText is empty while a placeholder shows).
    if (!node.displayText.empty())
        caretX += backend_.measure(run.text, s.fontSize, 0, font).width;

    Rect caret{caretX - rd::kCaretWidth / 2, r.y + rd::kCaretInset, rd::kCaretWidth, r.h - 2 * rd::kCaretInset};
    backend_.fillRect(caret, s.color, 0);
}

void TreeWalker::drawCanvas(const CanvasNode* node, const Rect& r) {
    if (!node->props.draw)
        return;
    // The backend isolates a throwing user callback (see IRenderBackend), so a
    // bad Canvas never unwinds through the walk or unbalances the clip stack.
    backend_.drawCanvas(*node, r);
}

}  // namespace

void renderTree(Node* root, IRenderBackend& backend, const ErrorHandler& onError) noexcept {
    if (!root)
        return;

    // Backstop: a draw exception is already isolated per-Canvas by the backend,
    // but anything else thrown during the walk must not escape into the
    // draw-time C boundary. The caller's endFrame() restores any backend state
    // (e.g. clip scopes) an unwind left behind.
    try {
        TreeWalker walker(backend, onError);
        walker.drawNode(root, 0, 0);
    } catch (const std::exception& e) {
        report(onError, "renderTree", &e);
    } catch (...) {
        report(onError, "renderTree", nullptr);
    }
}

}  // namespace yui::render
