#include <yui/core/Node.hpp>
#include <yui/core/RenderDefaults.hpp>
#include <yui/render/StyleResolver.hpp>
#include <yui/render/TextWrap.hpp>
#include <yui/render/TreeRenderer.hpp>

#include <exception>
#include <string>
#include <string_view>

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

// The run an Input displays and its color: the (possibly password-masked)
// displayText, else the placeholder in the placeholder color.
struct InputRun {
    std::string text;
    uint32_t color = 0;
};

InputRun inputDisplayRun(const InputNode& node, uint32_t textColor) {
    // Masking (password stars) lives on the node — InputNode::displayRun is
    // the one source the click-mapping geometry measures through too.
    if (!node.displayText.empty())
        return {node.displayRun(), textColor};
    if (node.props.placeholder)
        return {*node.props.placeholder, render_defaults::kPlaceholderColor};
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
    void drawScrollbar(const ScrollNode& node, ScrollAxis axis, const Rect& r);
    void drawCanvas(const CanvasNode* node, const Rect& r);

    void paintBoxChrome(const ResolvedBoxStyle& s, const Rect& r);
    void drawCaret(const InputNode& node, const Rect& content, const ResolvedInputStyle& s);

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
    // A view over the prop (empty ⇒ default face): no per-node-per-frame
    // std::string temporary on the draw path.
    std::string_view font = p.font ? std::string_view(*p.font) : std::string_view{};

    // Wrap and draw in the CONTENT box: Yoga hands the measure callback the
    // content width (available minus the node's own padding+border), so paint
    // must wrap at that same width — and start at the content origin — or a
    // padded Text would re-wrap at the wider border-box width and disagree with
    // the height layout reserved. Same width both sides also keeps the wrap
    // cache shared between layout and paint. wrap(false) paints one
    // unconstrained run ('\n' still breaks).
    const LayoutResult& l = node->layout;
    float maxWidth = p.wrap.value_or(true) ? l.contentWidth() : 0;
    const auto& runs = node->wrappedRuns(maxWidth, &backend_);
    float lineHeight = backend_.fontMetrics(s.fontSize, font).lineHeight;
    for (size_t i = 0; i < runs.size(); ++i) {
        const TextRun& run = runs[i];
        if (run.begin == run.end)
            continue;  // a blank line occupies its slot but draws nothing
        backend_.drawTextRun(p.text.substr(run.begin, run.end - run.begin), x + l.insetLeft,
                             y + l.insetTop + static_cast<float>(i) * lineHeight, s.fontSize, s.color, font);
    }
}

void TreeWalker::drawScroll(const ScrollNode* node, const Rect& r) {
    auto s = resolveBox(node->props, node->hovered, node->focused);
    paintBoxChrome(s, r);

    // Content lives in the PADDED viewport: the scroll's own insets shrink the
    // clip and place the content origin, so Scroll padding is honored the same
    // way Box padding is. layoutScrollContent laid the detached content root
    // against this same content width, and hitTest gates on this same rect.
    const LayoutResult& l = node->layout;
    Rect viewport{r.x + l.insetLeft, r.y + l.insetTop, l.contentWidth(), l.contentHeight()};
    backend_.pushClip(viewport, s.borderRadius);
    float childOffsetX = viewport.x - node->scrollOffsetX;
    float childOffsetY = viewport.y - node->scrollOffsetY;
    for (auto& child : node->children) {
        drawNode(child.get(), childOffsetX, childOffsetY);
    }
    backend_.popClip();

    // Overlay scrollbars, outside the content clip (they sit within the
    // viewport bounds anyway): a bar per overflowing axis, geometry from the
    // node so the drawn thumb IS the hit region the EventHandler drags.
    drawScrollbar(*node, ScrollAxis::Vertical, r);
    drawScrollbar(*node, ScrollAxis::Horizontal, r);
}

void TreeWalker::drawScrollbar(const ScrollNode& node, ScrollAxis axis, const Rect& r) {
    namespace rd = render_defaults;
    ScrollbarGeometry g = node.scrollbar(axis);
    if (!g.active)
        return;
    float radius = rd::kScrollbarThickness / 2;
    backend_.fillRect({r.x + g.track.x, r.y + g.track.y, g.track.w, g.track.h}, rd::kScrollbarTrackColor, radius);
    backend_.fillRect({r.x + g.thumb.x, r.y + g.thumb.y, g.thumb.w, g.thumb.h}, rd::kScrollbarThumbColor, radius);
}

void TreeWalker::drawInput(const InputNode* node, const Rect& r) {
    namespace rd = render_defaults;
    auto s = resolveInput(node->props, node->hovered, node->focused);

    backend_.fillRect(r, s.backgroundColor, s.borderRadius);
    if (s.borderWidth > 0)
        backend_.strokeRect(r, s.borderColor, s.borderRadius, s.borderWidth);

    std::string_view font = node->props.font ? std::string_view(*node->props.font) : std::string_view{};
    InputRun run = inputDisplayRun(*node, s.color);
    // Blink is node state driven by update(dt), not a wall clock — the renderer
    // just paints what the node says (see InputNode::updateBlink).
    bool caretShown = node->focused && node->caretVisible;
    if (run.text.empty() && !caretShown)
        return;

    // Text and caret live in the CONTENT box (border box minus the input's own
    // insets) and are clipped to it: overflowing text must not ride past the
    // input's chrome. The early return above keeps the clip stack untouched
    // when there is nothing to draw inside it.
    const LayoutResult& l = node->layout;
    Rect content{r.x + l.insetLeft, r.y + l.insetTop, l.contentWidth(), l.contentHeight()};
    // The whole run shifts left by the follow-scroll; the clip hides what
    // rides out. (textScrollX is 0 whenever the text fits — see
    // InputNode::scrollCaretIntoView — so a placeholder never shifts.)
    backend_.pushClip(content, s.borderRadius);
    if (!run.text.empty()) {
        backend_.drawTextRun(run.text, content.x + rd::kInputTextPad - node->textScrollX,
                             content.y + (content.h - s.fontSize) / 2, s.fontSize, run.color, font);
    }
    if (caretShown)
        drawCaret(*node, content, s);
    backend_.popClip();
}

void TreeWalker::drawCaret(const InputNode& node, const Rect& content, const ResolvedInputStyle& s) {
    namespace rd = render_defaults;
    // The caret sits after the display prefix before its byte index, shifted
    // by the same follow-scroll as the text run. The edit path keeps the
    // caret inside the content box via textScrollX (scrollCaretIntoView), so
    // no right-edge pin is needed here anymore.
    float caretX = content.x + rd::kInputTextPad + node.caretPrefixWidth(&backend_) - node.textScrollX;

    Rect caret{caretX - rd::kCaretWidth / 2, content.y + rd::kCaretInset, rd::kCaretWidth,
               content.h - 2 * rd::kCaretInset};
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
