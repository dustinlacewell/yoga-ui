#include "doctest.h"

#include <yui/layout/Placement.hpp>

using namespace yui::layout;

namespace {

// A 1000x700 window with an 8px margin — the usable column is [8, 692], i.e.
// 684px tall; the usable X band for a 220px panel is [8, 772].
Viewport vp() { return {1000.0f, 700.0f, 8.0f}; }

constexpr float W = 220.0f;  // a typical menu width

}  // namespace

// ─── clampRange ──────────────────────────────────────────────────────────────

TEST_CASE("clampRange: inside, below, above, and empty range") {
    CHECK(clampRange(50, 0, 100) == 50);   // inside
    CHECK(clampRange(-5, 0, 100) == 0);    // below lo
    CHECK(clampRange(150, 0, 100) == 100); // above hi
    // Empty range (hi < lo): the panel is bigger than the space. Prefer lo so the
    // near edge stays visible rather than snapping to a nonsensical hi.
    CHECK(clampRange(50, 100, 0) == 100);
    CHECK(clampRange(-5, 100, 0) == 100);
}

// ─── placePanelY: move, don't shrink ─────────────────────────────────────────

TEST_CASE("placePanelY: short panel near the top keeps its anchor") {
    // Anchor at y=100, content 200 tall, fits easily → no shift.
    auto p = placePanelY(100, 200, vp());
    CHECK(p.y == doctest::Approx(100));
    CHECK(p.height == doctest::Approx(200));
}

TEST_CASE("placePanelY: panel near the bottom shifts UP, full height kept") {
    // Anchor at y=600, content 200 tall. Bottom would be 800 > 692, so shift up
    // until the bottom rests at 692 → y = 492. Height unchanged (NOT scrolled).
    auto p = placePanelY(600, 200, vp());
    CHECK(p.height == doctest::Approx(200));         // full height preserved
    CHECK(p.y == doctest::Approx(692 - 200));        // 492
    CHECK(p.y + p.height == doctest::Approx(692));   // bottom at the margin
}

TEST_CASE("placePanelY: panel taller than the column fills it and scrolls") {
    // Content 900 > usable column 684: this is the ONLY case height shrinks.
    auto p = placePanelY(100, 900, vp());
    CHECK(p.height == doctest::Approx(684));   // clamped to the column
    CHECK(p.y == doctest::Approx(8));          // pinned to the top margin
}

TEST_CASE("placePanelY: anchor above the top margin is pushed down to it") {
    auto p = placePanelY(-50, 100, vp());
    CHECK(p.y == doctest::Approx(8));
    CHECK(p.height == doctest::Approx(100));
}

TEST_CASE("placePanelY: fixedMaxH caps content before the column rule") {
    // Natural content 600, but capped to 300. The cap applies first, then the
    // (already-small) panel fits with no shift.
    auto p = placePanelY(100, 600, vp(), 300);
    CHECK(p.height == doctest::Approx(300));
    CHECK(p.y == doctest::Approx(100));
}

// ─── placePanel: both axes ───────────────────────────────────────────────────

TEST_CASE("placePanel: cursor in the bottom-right corner shifts up and left") {
    // Open a 220x200 panel at (950, 650). X: 950+220=1170 > 772 cap → clamp to
    // 772. Y: bottom 850 > 692 → shift up to 492.
    auto r = placePanel({950, 650}, {W, 200}, vp());
    CHECK(r.x == doctest::Approx(1000 - W - 8));   // 772
    CHECK(r.y == doctest::Approx(692 - 200));      // 492
    CHECK(r.height == doctest::Approx(200));
}

TEST_CASE("placePanel: top-left corner is clamped to the margins") {
    auto r = placePanel({-100, -100}, {W, 200}, vp());
    CHECK(r.x == doctest::Approx(8));
    CHECK(r.y == doctest::Approx(8));
}

// ─── chooseSideX: side-and-clamp in one decision ─────────────────────────────

TEST_CASE("chooseSideX: prefers the right when the submenu fits there") {
    // Parent at x=100,w=220 → right edge 320. Submenu 220 wide fits (320+220=540
    // <= 992). Opens flush to the right.
    Rect parent{100, 0, W, 100};
    CHECK(chooseSideX(parent, W, vp(), Side::Right) == doctest::Approx(320));
}

TEST_CASE("chooseSideX: flips left when the right side overflows") {
    // Parent's right edge at 820 → 820+220=1040 > 992, right doesn't fit. Left
    // edge 600-220=380 >= 8, left fits. Opens flush to the left at 380.
    Rect parent{600, 0, W, 100};
    CHECK(chooseSideX(parent, W, vp(), Side::Right) == doctest::Approx(600 - W));
}

TEST_CASE("chooseSideX: honors a Left preference when it fits") {
    Rect parent{600, 0, W, 100};
    // Prefer left first; left fits (380 >= 8) → 380, even though right also would
    // not fit here anyway.
    CHECK(chooseSideX(parent, W, vp(), Side::Left) == doctest::Approx(600 - W));
}

TEST_CASE("chooseSideX: neither side fits picks the side with more room, flush") {
    // Very wide submenu (900) against a centered parent: neither side fits. Right
    // room = 1000 - (x+w); left room = x. Choose the larger, place flush there.
    Rect parentRight{700, 0, W, 100};  // right room small, left room large
    // rightX = 920, leftX = 700-900 = -200. roomRight = 1000-920 = 80;
    // roomLeft = 700. left has more room → leftX (-200), flush, partial off-screen.
    CHECK(chooseSideX(parentRight, 900, vp(), Side::Right) == doctest::Approx(-200));

    Rect parentLeft{100, 0, W, 100};   // left room small, right room large
    // rightX = 320, leftX = 100-900 = -800. roomRight = 680; roomLeft = 100.
    // right has more room → rightX (320), flush.
    CHECK(chooseSideX(parentLeft, 900, vp(), Side::Right) == doctest::Approx(320));
}

TEST_CASE("chooseSideX: the flush submenu is NOT screen-clamped over its parent") {
    // The whole point of leaving X flush: a left-flipped submenu sits exactly at
    // parent.x - panelW, even if that's tight against the left margin, rather than
    // being clamped forward (which would overlap the parent). Here it lands at 8
    // by coincidence of geometry, but the key property is no overlap with parent.
    Rect parent{228, 0, W, 100};  // left edge 228, leftX = 228-220 = 8
    float x = chooseSideX(parent, W, vp(), Side::Right);
    // right: 448+220=668 <= 992 fits → opens right at 448, flush to parent's right.
    CHECK(x == doctest::Approx(448));
    CHECK(x >= parent.right());  // never overlaps the parent
}

// ─── placeSubmenu: side X + move-don't-shrink Y together ──────────────────────

TEST_CASE("placeSubmenu: cascades right and shifts up near the bottom") {
    Rect parent{300, 0, W, 100};
    // anchorY 600, content 200 → Y shifts up to 492 (same as placePanelY).
    // X: right edge 520, 520+220=740 <= 992 fits → 520.
    auto r = placeSubmenu(parent, 600, {W, 200}, vp(), Side::Right);
    CHECK(r.x == doctest::Approx(520));
    CHECK(r.y == doctest::Approx(492));
    CHECK(r.height == doctest::Approx(200));
}
