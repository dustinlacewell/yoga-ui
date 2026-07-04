#include "doctest.h"

#include <yui/yui.hpp>

#include <functional>
#include <string>
#include <vector>

using namespace yui;

namespace {

// First node with this key in pre-order (the test_modal idiom: reconciles
// shuffle child indexes, so tests re-find by key rather than by position).
Node* findByKey(Node* node, const std::string& key) {
    if (node->key == key)
        return node;
    for (auto& child : node->children)
        if (Node* found = findByKey(child.get(), key))
            return found;
    return nullptr;
}

// The shared fixture app: a 100x40 target at (100,100) (the outer padding
// keeps it off the viewport-inset clamps), tipped with a keyed 120x30 panel.
VNode app(float delayMs = 0) {
    auto tooltip =
        widgets::Tooltip(Box().width(100).height(40).setKey("target")).tip(Box().width(120).height(30).setKey("tip"));
    if (delayMs > 0)
        tooltip.delayMs(delayMs);
    return Box(std::move(tooltip)).width(800).height(600).padding(100);
}

}  // namespace

// ---------------------------------------------------------------------------
// Show/hide lifecycle: the tip mounts after the hover delay — in the SAME
// update whose advanceClock crosses the deadline (the clock ticks before the
// dirty checks in Host::update) — and unmounts when the pointer leaves.
// ---------------------------------------------------------------------------

TEST_CASE("Tooltip - shows after the hover delay (same update), placed below the target") {
    Host host;
    host.setRender(std::function<VNode()>([] { return app(); }));
    host.update(800, 600);

    // Hover the target: nothing shows before the deadline.
    host.handleMouseMove(150, 120);
    CHECK(findByKey(host.root(), "tip") == nullptr);
    host.update(800, 600, 0.3f);
    CHECK(findByKey(host.root(), "tip") == nullptr);

    // Crossing the deadline mounts the tip within this ONE update call.
    host.update(800, 600, 0.201f);
    Node* tip = findByKey(host.root(), "tip");
    REQUIRE(tip != nullptr);

    // Placed just below the target's wrapper: (100, 140 + kTooltipGap), in
    // root space (getBoundingRect anchor -> placePanel).
    auto r = absoluteRect(tip);
    CHECK(r.x == doctest::Approx(100));
    CHECK(r.y == doctest::Approx(140 + widgets::kTooltipGap));

    // Leaving both target and tip hides it.
    host.handleMouseMove(400, 400);
    host.update(800, 600, 0.016f);
    CHECK(findByKey(host.root(), "tip") == nullptr);

    // Re-entering re-arms and shows again after a fresh full delay.
    host.handleMouseMove(150, 120);
    host.update(800, 600, 0.3f);
    CHECK(findByKey(host.root(), "tip") == nullptr);  // fresh deadline, not 200ms leftover
    host.update(800, 600, 0.201f);
    CHECK(findByKey(host.root(), "tip") != nullptr);
}

TEST_CASE("Tooltip - pointer travel into the tip keeps it open; leaving the tip hides it") {
    Host host;
    host.setRender(std::function<VNode()>([] { return app(); }));
    host.update(800, 600);

    host.handleMouseMove(150, 120);
    host.update(800, 600, 0.501f);
    REQUIRE(findByKey(host.root(), "tip") != nullptr);

    // A SLOW target->tip drag: the target wrapper spans y 100-140, the flush
    // tip starts at y=140. Step through the boundary pixel-by-pixel — every
    // sample must land over EITHER the target OR the tip (no dead zone), so
    // the wrapper stays hovered and the tip never flickers off. (A nonzero
    // gap would leave y=141..143 over neither rect — the bug this pins. A
    // single 120->150 jump would skip that zone and never catch it.)
    for (float y = 138; y <= 150; y += 1) {
        host.handleMouseMove(150, y);
        host.update(800, 600, 0.016f);
        CHECK(findByKey(host.root(), "tip") != nullptr);
    }

    // Back from the tip onto the target, stepping through the boundary again.
    for (float y = 150; y >= 120; y -= 1) {
        host.handleMouseMove(150, y);
        host.update(800, 600, 0.016f);
        CHECK(findByKey(host.root(), "tip") != nullptr);
    }

    // From the tip out to open space: the wrapper finally leaves — hide.
    host.handleMouseMove(150, 150);
    host.update(800, 600, 0.016f);
    host.handleMouseMove(400, 400);
    host.update(800, 600, 0.016f);
    CHECK(findByKey(host.root(), "tip") == nullptr);
}

// ---------------------------------------------------------------------------
// The zero-size portal anchor: portal content roots lay out against the
// viewport, so the tip rides a 0x0 anchored point it overflows — the anchor
// itself is never hittable and everything outside the tip's own rect falls
// through to the tree behind (a tooltip is not a modal).
// ---------------------------------------------------------------------------

TEST_CASE("Tooltip - the open tip does not shadow clicks or hover outside its own rect") {
    Host host;
    int behindClicks = 0;
    host.setRender(std::function<VNode()>([&] {
        std::vector<Child> kids;
        kids.push_back(widgets::Tooltip(Box().width(100).height(40).setKey("target"))
                           .tip(Box().width(120).height(30).setKey("tip")));
        kids.push_back(Box().width(100).height(50).setKey("behind").onClick([&] { behindClicks++; }));
        return Box(std::move(kids)).width(800).height(600).padding(100);
    }));
    host.update(800, 600);

    host.handleMouseMove(150, 120);
    host.update(800, 600, 0.501f);
    REQUIRE(findByKey(host.root(), "tip") != nullptr);

    // The sibling below the target sits at (100,140)-(200,190); the tip covers
    // (100,144)-(220,174) of it. A click BESIDE the tip on the sibling must
    // land there — the tip's portal anchor is 0x0 and swallows nothing.
    host.handleMouseDown(150, 185, MouseButton::Left);
    host.handleMouseUp(150, 185, MouseButton::Left);
    CHECK(behindClicks == 1);
}

TEST_CASE("Tooltip - delayMs overrides the default hover delay") {
    Host host;
    host.setRender(std::function<VNode()>([] { return app(200); }));
    host.update(800, 600);

    host.handleMouseMove(150, 120);
    host.update(800, 600, 0.15f);
    CHECK(findByKey(host.root(), "tip") == nullptr);
    host.update(800, 600, 0.051f);  // 201ms: past the custom 200ms deadline
    CHECK(findByKey(host.root(), "tip") != nullptr);
}
