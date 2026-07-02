#pragma once

#include "Measure.hpp"
#include "Props.hpp"
#include "VNode.hpp"

#include "../layout/Placement.hpp"  // layout::Rect (scrollbar geometry, scrollIntoView)
#include "../render/TextWrap.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <yoga/Yoga.h>

namespace yui {

// Upper bound on tree depth for the recursive structural walks (event dispatch,
// hit testing, key-target search). These descend/bubble one native stack frame
// per tree level; a pathologically deep, data-driven tree could otherwise
// overflow the stack — a crash reachable from the public input API. 1024 is far
// beyond any realistic UI nesting yet a small fraction of the few-thousand-frame
// budget a native stack affords these tiny functions, so it diagnoses the
// degenerate case via the error sink long before the stack is at risk. The
// reconciler's VNode walk is NOT guarded here (aborting mid-reconcile would leave
// the tree partially built); its depth bound is documented as a precondition on
// the render API (see Host::setRender).
constexpr int kMaxTreeDepth = 1024;

// Computed layout result from Yoga. left/top/width/height are the BORDER box;
// the insets are the per-edge padding+border Yoga resolved, synced alongside so
// draw-time consumers get the content box without reaching back into the yoga
// node. Text wraps and draws in the content box — the same width Yoga hands
// the measure callback — so measure and paint agree for padded nodes too.
//
// subtree* is the subtree AABB: the union of this node's border box and every
// descendant's subtree bounds, in the SAME parent-relative space as left/top.
// Children draw unclipped, so one may extend beyond its parent's rect; hit
// testing PRUNES descent by this box (keeping overflowing children clickable)
// while a node is only itself HIT by its own border box. A Scroll clips its
// content, so its subtree bounds are exactly its own rect (see
// Node::syncLayoutFromYoga), keeping clipped overflow unhittable.
struct LayoutResult {
    float left = 0;
    float top = 0;
    float width = 0;
    float height = 0;
    float insetLeft = 0;
    float insetTop = 0;
    float insetRight = 0;
    float insetBottom = 0;
    float subtreeLeft = 0;
    float subtreeTop = 0;
    float subtreeRight = 0;
    float subtreeBottom = 0;

    float contentWidth() const { return width - insetLeft - insetRight; }
    float contentHeight() const { return height - insetTop - insetBottom; }
};

// Result of one frame's animation advance, folded up the subtree walk by
// Node::update. `animating` means keep pumping update() (an animation is in
// progress); `needsRepaint` means THIS advance changed pixels. The two are
// distinct: a smooth scroll repaints every animated frame, but a blinking
// caret animates every frame while repainting only on the visible↔hidden edge.
struct AnimationResult {
    bool animating = false;
    bool needsRepaint = false;
};

// Base class for all rendered nodes
class Node {
public:
    virtual ~Node();

    // Key constants (must match VNode)
    static constexpr int64_t NO_INT_KEY = INT64_MIN;
    static constexpr size_t NO_SOURCE_POSITION = SIZE_MAX;

    // Identity
    virtual PrimitiveType type() const = 0;
    std::string key;
    int64_t intKey = NO_INT_KEY;
    size_t sourcePosition = NO_SOURCE_POSITION;

    // Key helpers
    bool hasKey() const { return intKey != NO_INT_KEY || !key.empty(); }
    bool hasIntKey() const { return intKey != NO_INT_KEY; }
    bool hasStringKey() const { return !key.empty(); }

    // Props update (called by reconciler). Takes the new props by rvalue: the
    // VNode is fully consumable after reconcile (nothing reads it later), so the
    // owned props are MOVED into the node instead of copied.
    virtual void updateProps(PropsVariant&& props) = 0;

    // Tree structure
    std::vector<std::unique_ptr<Node>> children;
    Node* parent = nullptr;

    // Layout — every node has a valid yogaNode
    YGNodeRef yogaNode = nullptr;
    LayoutResult layout;

    // Interactive state (updated by EventHandler)
    bool hovered = false;
    bool focused = false;

    // Liveness token shared with holders that outlive this node — notably the
    // EventHandler's focusedInput_, which keeps a raw InputNode* that a
    // reconciliation may free out from under it. Cleared in ~Node so those
    // holders observe a dead token and treat the pointer as gone instead of
    // dereferencing freed memory. Mirrors the Fiber/Store/Host alive_ idiom:
    // observe via a weak_ptr copy, verify liveness before touching the node.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    // Apply layout props to yoga node
    void applyLayoutProps(const LayoutProps& props);

    // Sync layout results from yoga after calculation
    void syncLayoutFromYoga();

    // Calculate layout for this subtree
    void calculateLayout(float availableWidth, float availableHeight);

    // Advance animations (smooth scrolling, caret blink) for this subtree.
    AnimationResult update(float dt);

protected:
    // The yoga node is created against the host's config so its measure
    // callback can recover the host's text measurer from the config context.
    explicit Node(YGConfigRef config);
};

// Concrete node types
class BoxNode : public Node {
public:
    explicit BoxNode(YGConfigRef config);
    PrimitiveType type() const override { return PrimitiveType::Box; }
    void updateProps(PropsVariant&& props) override;

    BoxProps props;
};

// Last wrap result of a text-bearing node, keyed by (measurer, maxWidth);
// content/font/size changes invalidate it at their update site, and a measurer
// swap misses on the key. Yoga may probe several widths during one layout —
// only the last is kept; a miss just recomputes. The measurer key is POINTER
// identity — the same sanctioned limitation as the measurer liveness design in
// Measure.hpp: a destroyed measurer whose address is reused by a new one can
// false-hit until any content/font/size change. Shared by TextNode and the
// multiline InputNode so both cache the one wrap layout and paint agree on.
struct WrapCache {
    const ITextMeasurer* measurer = nullptr;
    float maxWidth = 0;
    bool valid = false;
    std::vector<render::TextRun> runs;
};

class TextNode : public Node {
public:
    explicit TextNode(YGConfigRef config);
    PrimitiveType type() const override { return PrimitiveType::Text; }
    void updateProps(PropsVariant&& props) override;

    TextProps props;

    // The wrap of props.text at maxWidth (0 = no soft wrap) under `measurer`
    // (nullptr ⇒ the fallback heuristic). Both the Yoga measure callback and the
    // tree renderer read through this, so layout and paint share ONE wrap — and
    // an unchanged node repaints without re-wrapping.
    const std::vector<render::TextRun>& wrappedRuns(float maxWidth, const ITextMeasurer* measurer) const;

private:
    void setupMeasureFunc();
    static YGSize measureFunc(YGNodeConstRef node, float width, YGMeasureMode widthMode, float height,
                              YGMeasureMode heightMode);

    // mutable: filled from const paths (Yoga's measure callback, the const
    // draw walk). Invalidated by text/fontSize/font/wrap changes in updateProps.
    mutable WrapCache wrapCache_;
};

class InputNode : public Node {
public:
    explicit InputNode(YGConfigRef config);
    PrimitiveType type() const override { return PrimitiveType::Input; }
    void updateProps(PropsVariant&& props) override;

    InputProps props;

    // Display text - synced from props.value, modified during editing
    std::string displayText;

    // Caret position as a BYTE offset into displayText. Invariant: always on a
    // UTF-8 code-point boundary — every editing op moves by whole code points,
    // and an external value change re-clamps/snaps it (see updateProps).
    //
    // Initializes to 0 on mount and is NOT auto-moved to the end on focus: a
    // mouse focus-click places it at the clicked boundary (click-to-position
    // in EventHandler::handleMouseDown); a KEYBOARD focus (Tab) leaves it at
    // the front until End/arrows reposition. Intentional.
    size_t caret = 0;

    // Selection anchor (byte offset, same boundary invariant). The selection
    // is [selBegin(), selEnd()) between the anchor and the caret; anchor ==
    // caret means no selection. Convention: the CARET is the MOVING end —
    // extend-moves and shift+click move the caret while the anchor stays put,
    // so the follow-scroll (which tracks the caret) keeps the end the user is
    // dragging visible.
    size_t selectionAnchor = 0;

    bool hasSelection() const { return selectionAnchor != caret; }
    size_t selBegin() const { return std::min(selectionAnchor, caret); }
    size_t selEnd() const { return std::max(selectionAnchor, caret); }
    void clearSelection() { selectionAnchor = caret; }

    // Horizontal follow-scroll (px): how far the text run is shifted LEFT so
    // the caret stays inside the content box. Maintained by
    // scrollCaretIntoView from the edit path; the renderer subtracts it from
    // both the text run's and the caret's x. Always 0 while the text fits.
    // For a multiline input it applies within the caret's LINE (wrapping keeps
    // lines inside the content width, so it is ~0 in practice).
    float textScrollX = 0;

    // Vertical follow-scroll (px), the textScrollX mirror for multiline: how
    // far the wrapped lines are shifted UP so the caret's line stays inside
    // the content box. Maintained by scrollCaretIntoView; always 0 for a
    // single-line input and whenever the lines fit (the auto-grown case).
    float textScrollY = 0;

    // Sticky goal column for vertical caret navigation (text-space x within a
    // line). The FIRST MoveUp/MoveDown records the caret's x here; consecutive
    // vertical moves keep steering toward it, so passing through a short line
    // does not lose the column. Invalidated (reset) by ANY other consumed
    // command, typed text, a click, or a drag — the next vertical move then
    // re-records from the caret's new position. Owned by the edit path
    // (EventHandler); the node just carries the state between commands.
    std::optional<float> verticalNavGoalX;

    // Caret blink state, advanced by update(dt) while focused. The renderer
    // draws the caret iff focused && caretVisible — no wall clock involved.
    bool caretVisible = true;

    // --- Text geometry (single-line) ---
    // Lives on the node so the renderer's caret/text placement and the event
    // handler's click mapping share ONE source (the ScrollNode scrollbar
    // precedent). All widths are measured with `m` (nullptr ⇒ the fallback
    // heuristic, matching TextNode::wrappedRuns) at the SAME resolved font the
    // renderer paints with. x coordinates are in TEXT SPACE: measured from the
    // left edge of the first glyph, pre-scroll — a window x maps in via
    //   textX = windowX - (absContentX + kInputTextPad) + textScrollX.

    // The string this input DISPLAYS for its value: displayText, or one '*'
    // per CODE POINT for a password. (The placeholder is chrome, not value —
    // geometry over an empty value is empty.) The single masking source; the
    // renderer's display run reads through this.
    std::string displayRun() const;

    // Width of the display run before byte offset `i` (clamped into range) —
    // an x in text space. THE prefix measurement: caret placement, selection
    // highlight edges, and click mapping all measure through it, so they can
    // never disagree. For a password the prefix is one star per code point of
    // the raw prefix (star space, matching what is drawn).
    float prefixWidthAt(size_t i, const ITextMeasurer* m) const;

    // The caret's x in text space: the prefix width at the caret.
    float caretPrefixWidth(const ITextMeasurer* m) const { return prefixWidthAt(caret, m); }

    // The caret byte offset (into displayText) nearest to `textX`. Midpoint
    // rule: the caret lands BEFORE a code point when textX <= that code
    // point's midpoint (prefix + width/2) — a click exactly ON a midpoint
    // resolves to the boundary on its LEFT. Prefix widths are whole-run
    // measurements (not per-cp sums), so the chosen boundary agrees exactly
    // with caretPrefixWidth under a kerning measurer. Clamps: textX <= 0 ⇒ 0,
    // past the last midpoint ⇒ displayText.size(). Always a cp boundary.
    //
    // Zero-advance code points (combining marks) are an inherent exception:
    // two boundaries share one x, so a click there resolves to the earlier
    // (ties-left) — a click can land on either boundary but never between them.
    size_t indexAtPoint(float textX, const ITextMeasurer* m) const;

    // The [begin, end) byte range of the same-class run — word (non-space) or
    // space run — at caret boundary `i` (clamped): double-click word selection.
    // "Same class" is the wrapText tokenization (space byte vs non-space byte,
    // see render::wrapText's wrapSegment) — ONE word-boundary definition shared
    // with wrapping, not a second one. A boundary between two runs takes the
    // run STARTING at it (the byte AT `i`); the end boundary takes the trailing
    // run. Byte-class scanning stops only on code-point boundaries by
    // construction: ' ' is a one-byte code point and continuation bytes are
    // non-space, so a class change is always a code-point start. Empty ⇒ {0,0}.
    std::pair<size_t, size_t> wordRangeAt(size_t i) const;

    // --- Text geometry (multiline) ---
    // Active iff multiline() (see InputProps::multiline; password wins
    // single-line). Lines are the wrapped runs of displayText — the SAME
    // render::wrapText the Yoga measure func sizes with and the renderer
    // paints, so caret geometry, layout, and paint share one line structure.

    // Is this input in multiline (textarea) mode? password && multiline is
    // unsupported (stars have no line structure): password wins single-line.
    bool multiline() const { return props.multiline.value_or(false) && !props.password.value_or(false); }

    // The soft-wrap constraint: the content width minus the text pad on both
    // sides (where the text actually draws). Reads the layout synced by the
    // LAST calculateLayout, like the rest of the edit-path geometry.
    float wrapWidth() const;

    // The wrapped lines of displayText at maxWidth under `m` (nullptr ⇒ the
    // fallback heuristic), cached like TextNode::wrappedRuns; text edits and
    // prop changes invalidate via markTextChanged/updateProps.
    const std::vector<render::TextRun>& displayRuns(float maxWidth, const ITextMeasurer* m) const;

    // Where the caret sits in the wrapped line structure: its line index and
    // its x within that line (measured from the line's own start — the origin
    // the renderer draws each line from). Soft-wrap affinity: a caret exactly
    // ON a soft-break boundary belongs to the END of the EARLIER line (a
    // documented simplification — there is one caret state per byte offset,
    // not an affinity bit). Single-line degenerates to {0, caretPrefixWidth}.
    struct CaretPlacement {
        size_t line = 0;
        float x = 0;
    };
    CaretPlacement caretPlacement(const ITextMeasurer* m) const;

    // Width of the text from the START of wrapped line `line` to byte
    // boundary `i` (clamped into the line's range) — the multiline analog of
    // prefixWidthAt, measured from the line's own origin because each line
    // draws as its own run at the text pad. Caret placement, the per-line
    // selection edges, and the vertical-move column all measure through it.
    float prefixWidthInLine(size_t line, size_t i, const ITextMeasurer* m) const;

    // The caret boundary nearest textX WITHIN wrapped line `line` (clamped
    // into range): the indexAtPoint midpoint rule applied to one line, with
    // widths measured from the line's start. Multiline only (multiline text
    // is never masked). Vertical moves target (goalX, line ∓ 1) through this.
    size_t indexAtLineX(size_t line, float textX, const ITextMeasurer* m) const;

    // Point form of the click mapping: pick the line from textY (clamped to
    // the first/last line, so clicks above/below the text land on the edge
    // lines), then the boundary within it. textY is text-space (window y
    // minus the content-box top, plus textScrollY). Single-line ignores
    // textY and delegates to the 1-D indexAtPoint.
    size_t indexAtPoint(float textX, float textY, const ITextMeasurer* m) const;

    // Text content changed under the caret (edit path or an external value
    // replacement): drop the cached wrap, and for a measure-func (multiline)
    // input mark the Yoga node dirty so the next layout re-measures the line
    // count. Single-line inputs have no measure func (YGNodeMarkDirty asserts
    // without one), so this is gated on the func's presence.
    void markTextChanged();

    // Follow the caret with textScrollX so it stays inside the visible text
    // span (the content box minus kInputTextPad on each side), clamped so no
    // dead space opens past the last glyph. Called from the edit path after
    // every caret/text mutation. Timing: reads the layout synced by the LAST
    // calculateLayout — edit events arrive between frames, after layout ran,
    // so the content box is current for the state being edited. Multiline
    // follows BOTH axes: x within the caret's line, textScrollY for the line.
    void scrollCaretIntoView(const ITextMeasurer* m);

    // Advance the blink phase by dt. Returns true when visibility toggled
    // (the repaint edge). Called from Node::update only while focused.
    bool updateBlink(float dt);

    // Restart the blink cycle so the caret shows immediately on focus gain.
    void resetCaretBlink() {
        blinkPhaseMs_ = 0;
        caretVisible = true;
    }

private:
    // Install/remove the Yoga measure func as multiline toggles (updateProps).
    // Only a multiline input measures — single-line sizing stays prop-driven,
    // so existing inputs see ZERO layout change.
    void syncMeasureFunc();
    static YGSize measureFunc(YGNodeConstRef node, float width, YGMeasureMode widthMode, float height,
                              YGMeasureMode heightMode);

    // The line containing byte boundary `i`: the FIRST run with i <= run.end
    // (the soft-wrap affinity rule — see caretPlacement).
    size_t lineOf(size_t i, const std::vector<render::TextRun>& runs) const;

    // mutable: filled from const paths (the measure callback, the draw walk).
    // Invalidated by markTextChanged and updateProps.
    mutable WrapCache wrapCache_;

    float blinkPhaseMs_ = 0;
};

// Which scrollbar of a Scroll node an operation refers to.
enum class ScrollAxis { Horizontal, Vertical };

// What a point inside a Scroll node landed on, scrollbar-wise. Thumb starts a
// drag gesture; track pages by a viewport; None falls through to content.
enum class ScrollbarPart { None, HorizontalTrack, HorizontalThumb, VerticalTrack, VerticalThumb };

// Geometry of one overlay scrollbar, in the scroll node's LOCAL space (origin =
// its border-box top-left). active is false when the axis doesn't overflow the
// padded viewport — the bar isn't drawn and eats no hits — and the rects are
// meaningful only while active. Computed on demand by ScrollNode::scrollbar so
// the renderer's bars and the event handler's hit regions share one source.
struct ScrollbarGeometry {
    bool active = false;
    layout::Rect track;
    layout::Rect thumb;
};

class ScrollNode : public Node {
public:
    explicit ScrollNode(YGConfigRef config);
    PrimitiveType type() const override { return PrimitiveType::Scroll; }
    void updateProps(PropsVariant&& props) override;

    ScrollProps props;

    // Scroll state
    float targetScrollX = 0;
    float targetScrollY = 0;
    float scrollOffsetX = 0;
    float scrollOffsetY = 0;

    // Content size (computed after layout)
    float contentWidth = 0;
    float contentHeight = 0;

    void updateContentSize();
    void clampScrollOffset();
    bool updateSmooth(float dt);

    // Programmatic scroll: set the target offset (clamped like every other
    // writer, via clampScrollOffset); the smooth interpolation animates there.
    void scrollTo(float x, float y);

    // Scroll the minimum needed to bring an ABSOLUTE-space rect (e.g. a
    // NodeRef::getBoundingRect) into the padded viewport; a rect already fully
    // visible changes nothing. A rect larger than the viewport aligns its
    // near (top/left) edge.
    void scrollIntoView(const layout::Rect& target);

    // Overlay scrollbar geometry along one axis (see ScrollbarGeometry).
    ScrollbarGeometry scrollbar(ScrollAxis axis) const;

    // Classify a point in LOCAL (border-box) coordinates against the active
    // scrollbars. The EventHandler routes thumb/track presses through this.
    ScrollbarPart scrollbarHitTest(float localX, float localY) const;

    // Scroll offset change per pixel of thumb travel along an axis
    // (maxScroll / free track length); 0 when the thumb cannot travel.
    float scrollPerThumbPixel(ScrollAxis axis) const;
};

class CanvasNode : public Node {
public:
    explicit CanvasNode(YGConfigRef config);
    PrimitiveType type() const override { return PrimitiveType::Canvas; }
    void updateProps(PropsVariant&& props) override;

    CanvasProps props;
};

// Factory: create Node from VNode type. The yoga node is created against the
// given config (may be nullptr, which resolves to Yoga's default config).
std::unique_ptr<Node> createNode(PrimitiveType type, YGConfigRef config);

}  // namespace yui
