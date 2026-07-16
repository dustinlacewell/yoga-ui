#include "doctest.h"

#include <yui/core/RenderDefaults.hpp>
#include <yui/detail/Reconciler.hpp>

using namespace yui;

namespace {

constexpr float kT = render_defaults::kScrollbarThickness;

// Mount a Scroll as the render root and lay it out at its own declared size.
// calculateLayout runs the gutter-reservation loop (layoutDetachedContent),
// so every assertion below reads settled post-layout state.
ScrollNode* layoutScroll(Reconciler& reconciler, VNode&& tree, float w, float h) {
    reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(w, h);
    REQUIRE(root->type() == PrimitiveType::Scroll);
    return static_cast<ScrollNode*>(root);
}

}  // namespace

TEST_CASE("scroll gutter: Auto reserves nothing when content fits") {
    Reconciler reconciler;
    auto* scroll = layoutScroll(
        reconciler, Scroll(Box().height(80).setKey("content")).width(100).height(100), 100, 100);

    CHECK_FALSE(scroll->gutterV);
    CHECK_FALSE(scroll->gutterH);
    CHECK(scroll->viewportWidth() == doctest::Approx(100));
    CHECK(scroll->viewportHeight() == doctest::Approx(100));
    // Content stretched to the full viewport width — no strip withheld.
    CHECK(scroll->children[0]->layout.width == doctest::Approx(100));
    CHECK_FALSE(scroll->scrollbar(ScrollAxis::Vertical).active);
    CHECK_FALSE(scroll->scrollbar(ScrollAxis::Horizontal).active);
}

TEST_CASE("scroll gutter: Auto reserves the vertical gutter on overflow and content re-lays narrower") {
    Reconciler reconciler;
    auto* scroll = layoutScroll(
        reconciler, Scroll(Box().height(300).setKey("content")).width(100).height(100), 100, 100);

    CHECK(scroll->gutterV);
    CHECK_FALSE(scroll->gutterH);
    CHECK(scroll->viewportWidth() == doctest::Approx(100 - kT));
    // The content root was RE-laid against the gutter-reduced width — the bar
    // owns its strip, content can't extend under it.
    CHECK(scroll->children[0]->layout.width == doctest::Approx(100 - kT));

    // The bar fills the gutter: track sits exactly in the reserved strip.
    ScrollbarGeometry v = scroll->scrollbar(ScrollAxis::Vertical);
    CHECK(v.active);
    CHECK(v.track.x == doctest::Approx(100 - kT));
    CHECK(v.track.y == doctest::Approx(0));
    CHECK(v.track.w == doctest::Approx(kT));
    CHECK(v.track.h == doctest::Approx(100));

    // Scroll range measures against the viewport, not the raw content box.
    scroll->scrollTo(0, 1e6f);
    CHECK(scroll->targetScrollY == doctest::Approx(300 - 100));
}

TEST_CASE("scroll gutter: Stable reserves the vertical gutter without overflow, bar stays hidden") {
    Reconciler reconciler;
    auto* scroll = layoutScroll(reconciler,
                                Scroll(Box().height(50).setKey("content"))
                                    .scrollbarGutter(ScrollbarGutter::Stable)
                                    .width(100)
                                    .height(100),
                                100, 100);

    CHECK(scroll->gutterV);
    CHECK(scroll->viewportWidth() == doctest::Approx(100 - kT));
    CHECK(scroll->children[0]->layout.width == doctest::Approx(100 - kT));
    // Reserved gutter, no overflow: blank strip, no bar, nothing to hit.
    CHECK_FALSE(scroll->scrollbar(ScrollAxis::Vertical).active);
    CHECK(scroll->scrollbarHitTest(100 - kT / 2, 50) == ScrollbarPart::None);
}

TEST_CASE("scroll gutter: horizontal reservation cascades into vertical (convergence)") {
    // Content 200x96 in a 100x100 scroll: fits vertically at first, but the
    // horizontal overflow reserves the bottom gutter, the viewport height
    // drops below 96, and the SECOND pass must reserve the vertical gutter
    // too — the monotonic loop, exercised end to end.
    Reconciler reconciler;
    auto* scroll = layoutScroll(
        reconciler,
        Scroll(Box().width(200).height(96).setKey("content")).width(100).height(100), 100, 100);

    CHECK(scroll->gutterH);
    CHECK(scroll->gutterV);
    CHECK(scroll->viewportWidth() == doctest::Approx(100 - kT));
    CHECK(scroll->viewportHeight() == doctest::Approx(100 - kT));

    // Each track spans its viewport axis, which already stops short of the
    // other bar's gutter — the shared corner square stays empty.
    ScrollbarGeometry v = scroll->scrollbar(ScrollAxis::Vertical);
    ScrollbarGeometry h = scroll->scrollbar(ScrollAxis::Horizontal);
    CHECK(v.active);
    CHECK(h.active);
    CHECK(v.track.h == doctest::Approx(100 - kT));
    CHECK(h.track.w == doctest::Approx(100 - kT));
    CHECK(v.track.x == doctest::Approx(100 - kT));
    CHECK(h.track.y == doctest::Approx(100 - kT));

    // Both ranges clamp against the reduced viewport.
    scroll->scrollTo(1e6f, 1e6f);
    CHECK(scroll->targetScrollX == doctest::Approx(200 - (100 - kT)));
    CHECK(scroll->targetScrollY == doctest::Approx(96 - (100 - kT)));
}

TEST_CASE("scroll gutter: scrollbarThickness overrides the reserved gutter and bar width") {
    Reconciler reconciler;
    auto* scroll = layoutScroll(reconciler,
                                Scroll(Box().height(300).setKey("content"))
                                    .scrollbarThickness(4)
                                    .width(100)
                                    .height(100),
                                100, 100);

    // The override drives gutter reservation, viewport, and bar geometry — not
    // the kScrollbarThickness default.
    CHECK(scroll->scrollbarThickness() == doctest::Approx(4));
    CHECK(scroll->viewportWidth() == doctest::Approx(96));
    CHECK(scroll->children[0]->layout.width == doctest::Approx(96));
    ScrollbarGeometry v = scroll->scrollbar(ScrollAxis::Vertical);
    CHECK(v.active);
    CHECK(v.track.x == doctest::Approx(96));
    CHECK(v.track.w == doctest::Approx(4));
}

TEST_CASE("scroll gutter: bar sits inside the scroll's own padding") {
    Reconciler reconciler;
    auto* scroll = layoutScroll(
        reconciler,
        Scroll(Box().height(300).setKey("content")).padding(4).width(100).height(100), 100, 100);

    // Padded content box is 92 wide; the gutter comes off ITS right edge, so
    // the track spans [insetLeft + viewport, insetLeft + viewport + kT) —
    // flush against the right padding band, never inside it.
    CHECK(scroll->viewportWidth() == doctest::Approx(92 - kT));
    ScrollbarGeometry v = scroll->scrollbar(ScrollAxis::Vertical);
    CHECK(v.active);
    CHECK(v.track.x == doctest::Approx(4 + 92 - kT));
    CHECK(v.track.y == doctest::Approx(4));
    CHECK(v.track.h == doctest::Approx(92));
    CHECK(scroll->children[0]->layout.width == doctest::Approx(92 - kT));
}
