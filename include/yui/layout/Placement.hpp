#pragma once

#include <algorithm>

// Floating-panel placement geometry.
//
// Laying out content WITHIN a panel is Yoga's job. Placing a floating panel —
// a context menu, dropdown, cascading submenu, tooltip — at an absolute screen
// position is not: the consumer must decide where the panel's top-left goes so
// it stays on-screen near every edge. That geometry is the same for everyone and
// easy to get subtly wrong, so it lives here as pure free functions.
//
// These functions take plain rects and sizes and return a position; they touch
// no yui render types. The caller feeds the result into .positionLeft()/
// .positionTop() on its own absolutely-positioned panel, keeping the panel's
// structure (the Box, the Scroll, the backdrop, the chrome) entirely its own.
//
// Two hard-won design rules, each from a real menu-integration bug:
//
//   1. Move, don't shrink. A panel that would overflow the bottom edge is
//      shifted UP so its bottom rests at the margin, keeping full height — it is
//      NOT clamped to the space below its anchor and made to scroll. Scrolling
//      happens ONLY when the panel is genuinely taller than the whole window
//      column (placePanelY).
//
//   2. Side-and-clamp is ONE decision, not two passes. A separate "flip" pass
//      and "clamp" pass each look right alone but compose into an overlap near an
//      edge. chooseSideX decides the side AND yields a final X in a single step;
//      placePanel only backstops onto the screen. Never split them.
//
// Anchors are DRAWN rects — a panel/item's true on-screen rect, with scroll and
// any prior clamping already baked in — not logical/content-relative offsets.
// Taking the drawn rect forces the caller into the correct input and makes the
// classic "submenu mis-anchored off a scrolled parent" bug unrepresentable.

namespace yui::layout {

// An axis-aligned rectangle in screen coordinates (top-left origin).
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    float right() const { return x + w; }
    float bottom() const { return y + h; }
};

// A 2D size or point.
struct Vec {
    float x = 0, y = 0;
};

// The window/viewport the panel must stay inside, plus the inset to keep from
// each edge. Construct once per placement pass and thread through.
//
// Edges are independent so a host can reserve space asymmetrically — e.g. a menu
// bar that owns the top strip sets `top = BAR_HEIGHT` while the other edges stay
// small. For the common "same inset on every side" case use Viewport::uniform.
struct Viewport {
    float width = 0;
    float height = 0;
    float top = 8;     // keep panels this far below the top edge
    float right = 8;   // ...left of the right edge
    float bottom = 8;  // ...above the bottom edge
    float left = 8;    // ...right of the left edge

    // Same inset on all four edges (the default for a plain window).
    static Viewport uniform(float width, float height, float inset = 8) {
        return {width, height, inset, inset, inset, inset};
    }
};

// Which side of an anchor a cascading submenu prefers to open toward.
enum class Side { Right, Left };

// Clamp v into [lo, hi]. If the range is empty (hi < lo, i.e. the panel is wider
// or taller than the space), prefer lo so the panel's near edge stays visible.
inline float clampRange(float v, float lo, float hi) {
    if (hi < lo) return lo;
    return std::max(lo, std::min(v, hi));
}

// Result of vertical placement: the top Y to render at, and the height the panel
// is allowed to occupy (which equals the desired height unless the panel had to
// shrink to fit the window column, in which case the panel should scroll).
struct PlacedY {
    float y = 0;
    float height = 0;
};

// Vertical "move, don't shrink" placement.
//
//   anchorY     the top the panel wants (e.g. a cursor Y, or a parent row's
//               drawn top for a cascade)
//   contentH    the panel's natural content height
//   vp          the viewport (height + top/bottom insets)
//   fixedMaxH   optional hard cap on content height (e.g. an explicit maxHeight);
//               0 means no cap
//
// The panel keeps full height and is shifted UP so its bottom rests at the
// bottom inset, rather than being clamped below the anchor and scrolled. It
// shrinks to the window column (and thus scrolls) ONLY when taller than the
// whole column. This is the single source of the vertical math.
inline PlacedY placePanelY(float anchorY, float contentH, const Viewport& vp,
                           float fixedMaxH = 0) {
    if (fixedMaxH > 0)
        contentH = std::min(contentH, fixedMaxH);

    const float topLimit = vp.top;
    const float bottomLimit = vp.height - vp.bottom;
    const float columnH = bottomLimit - topLimit;

    float height = std::min(contentH, columnH);
    float y = clampRange(anchorY, topLimit, bottomLimit - height);
    return {y, height};
}

// Choose the X for a cascading submenu opening off a parent panel, deciding the
// side AND the position in one pass (rule 2). The submenu opens flush to the
// parent's edge — never overlapping it.
//
//   parent      the parent panel's DRAWN rect (its real on-screen x/width)
//   panelW      the submenu's width
//   vp          the viewport (width + left/right insets)
//   prefer      the side to try first (Right for a left-to-right cascade)
//
// Prefer the side the submenu fully fits. If the preferred side doesn't fit but
// the other does, use the other. If neither fits, pick the side with more room
// and place flush there (accept partial off-screen, never overlap the parent).
//
// Returns the submenu's left X. It is intentionally NOT clamped onto the screen:
// clamping a deliberately-flush submenu would shove it back over its parent. The
// vertical axis still clamps via placePanelY; only this X is left flush.
inline float chooseSideX(const Rect& parent, float panelW, const Viewport& vp,
                         Side prefer = Side::Right) {
    const float rightX = parent.right();      // submenu left edge, opening right
    const float leftX = parent.x - panelW;    // submenu left edge, opening left
    const bool rightFits = (rightX + panelW <= vp.width - vp.right);
    const bool leftFits = (leftX >= vp.left);

    const float roomRight = vp.width - rightX;  // space to the right of the parent
    const float roomLeft = parent.x;            // space to the left of the parent

    const bool tryRightFirst = (prefer == Side::Right);
    const float firstX = tryRightFirst ? rightX : leftX;
    const float secondX = tryRightFirst ? leftX : rightX;
    const bool firstFits = tryRightFirst ? rightFits : leftFits;
    const bool secondFits = tryRightFirst ? leftFits : rightFits;

    if (firstFits) return firstX;
    if (secondFits) return secondX;
    // Neither side fits: pick the side with more room, flush to the parent.
    return (roomRight >= roomLeft) ? rightX : leftX;
}

// Result of a full placement: the top-left to render the panel at, plus the
// height it may occupy (== desired unless the panel shrank to scroll).
struct PlacedRect {
    float x = 0, y = 0, height = 0;
};

// Place a free-standing panel (a context menu or top-level dropdown) at an
// anchor point, clamping both axes onto the screen.
//
//   anchor      the point the panel opens at (e.g. the cursor); the panel's
//               top-left starts here, then clamps
//   panelSize   the panel's {width, contentHeight}
//   vp          the viewport
//   fixedMaxH   optional content-height cap (0 = none)
//
// X is clamped so the panel stays fully on-screen; Y uses placePanelY's
// move-don't-shrink rule. For a panel that should open its right edge at the
// anchor when it would overflow right (a cursor menu near the right edge), pass
// the already-flipped anchor.x, or use chooseSideX for the cascade case.
inline PlacedRect placePanel(Vec anchor, Vec panelSize, const Viewport& vp,
                             float fixedMaxH = 0) {
    auto py = placePanelY(anchor.y, panelSize.y, vp, fixedMaxH);
    float x = clampRange(anchor.x, vp.left, vp.width - panelSize.x - vp.right);
    return {x, py.y, py.height};
}

// Place a cascading submenu off a parent panel: side-aware X (flush, never
// overlapping) plus move-don't-shrink Y, in one call. This is the cascade
// one-liner — feed it the parent's drawn rect and the submenu's natural height.
//
//   parent      the parent panel's (or hovered row's) DRAWN rect
//   anchorY     the Y the submenu should open at (typically the parent row's
//               drawn top, so the submenu's first item aligns with it)
//   panelSize   the submenu's {width, contentHeight}
//   vp          the viewport
//   prefer      preferred cascade side (Right for left-to-right)
//   fixedMaxH   optional content-height cap (0 = none)
//
// The X is left flush to the parent (not screen-clamped) so it can't be shoved
// back over the parent; the Y still clamps and shifts up near the bottom edge.
inline PlacedRect placeSubmenu(const Rect& parent, float anchorY, Vec panelSize,
                               const Viewport& vp, Side prefer = Side::Right,
                               float fixedMaxH = 0) {
    float x = chooseSideX(parent, panelSize.x, vp, prefer);
    auto py = placePanelY(anchorY, panelSize.y, vp, fixedMaxH);
    return {x, py.y, py.height};
}

}  // namespace yui::layout
