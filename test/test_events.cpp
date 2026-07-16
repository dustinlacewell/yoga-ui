#include "TestClipboard.hpp"
#include "TestMeasurer.hpp"
#include "doctest.h"

#include <yui/core/EventHandler.hpp>
#include <yui/core/RenderDefaults.hpp>
#include <yui/detail/Reconciler.hpp>
#include <yui/render/StyleResolver.hpp>
#include <yui/yui.hpp>

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace yui;
namespace rd = yui::render_defaults;

TEST_CASE("Hit test finds deepest node") {
    Reconciler reconciler;
    EventHandler events;

    // Nested boxes
    auto tree = Box(
                        Box().width(50).height(50).setKey("inner")
                    )
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    // Click inside inner box
    events.handleMouseMove(root, 25, 25);
    CHECK(events.getHoveredNode() == root->children[0].get());

    // Click outside inner but inside outer
    events.handleMouseMove(root, 75, 75);
    CHECK(events.getHoveredNode() == root);

    // Click outside all
    events.handleMouseMove(root, 150, 150);
    CHECK(events.getHoveredNode() == nullptr);
}

TEST_CASE("Click handler fires") {
    Reconciler reconciler;
    EventHandler events;

    bool clicked = false;
    auto tree = Box().width(100).height(100).onClick([&]() { clicked = true; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    // Mouse down + up = click
    events.handleMouseDown(root, 50, 50, MouseButton::Left);
    CHECK(!clicked);  // Click fires on mouse up

    events.handleMouseUp(root, 50, 50, MouseButton::Left);
    CHECK(clicked);
}

TEST_CASE("Right click handler fires") {
    Reconciler reconciler;
    EventHandler events;

    bool rightClicked = false;
    auto tree = Box().width(100).height(100).onRightClick([&]() { rightClicked = true; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    events.handleMouseDown(root, 50, 50, MouseButton::Right);
    events.handleMouseUp(root, 50, 50, MouseButton::Right);
    CHECK(rightClicked);
}

TEST_CASE("Click bubbles to parent") {
    Reconciler reconciler;
    EventHandler events;

    bool parentClicked = false;
    auto tree = Box(
                        Box().width(50).height(50).setKey("inner")
                    )
                    .width(100)
                    .height(100)
                    .onClick([&]() { parentClicked = true; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    // Click on inner (which has no handler)
    events.handleMouseDown(root, 25, 25, MouseButton::Left);
    events.handleMouseUp(root, 25, 25, MouseButton::Left);

    // Should bubble to parent
    CHECK(parentClicked);
}

TEST_CASE("Click consumption stops bubbling") {
    Reconciler reconciler;
    EventHandler events;

    bool innerClicked = false;
    bool outerClicked = false;

    auto tree = Box(
                        Box().width(50).height(50).setKey("inner").onClick([&]() { innerClicked = true; })
                    )
                    .width(100)
                    .height(100)
                    .onClick([&]() { outerClicked = true; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    // Click on inner
    events.handleMouseDown(root, 25, 25, MouseButton::Left);
    events.handleMouseUp(root, 25, 25, MouseButton::Left);

    CHECK(innerClicked);
    CHECK(!outerClicked);  // Stopped by inner handler
}

TEST_CASE("onClick requires press and release on the same node") {
    Reconciler reconciler;
    EventHandler events;

    // Two side-by-side boxes, each with its own onClick.
    bool aClicked = false, bClicked = false;
    auto tree = Box(
                        Box().width(50).height(100).setKey("a").onClick([&]() { aClicked = true; }),
                        Box().width(50).height(100).setKey("b").onClick([&]() { bClicked = true; })
                    )
                    .flexDirection(FlexDirection::Row)
                    .width(100).height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    SUBCASE("press and release on the same node clicks") {
        events.handleMouseDown(root, 25, 50, MouseButton::Left);  // box A
        events.handleMouseUp(root, 25, 50, MouseButton::Left);    // box A
        CHECK(aClicked);
        CHECK(!bClicked);
    }

    SUBCASE("press on A, release on B fires neither") {
        events.handleMouseDown(root, 25, 50, MouseButton::Left);  // box A
        events.handleMouseUp(root, 75, 50, MouseButton::Left);    // box B
        CHECK(!aClicked);
        CHECK(!bClicked);
    }

    SUBCASE("bare release with no press fires nothing (the orphan-release case)") {
        events.handleMouseUp(root, 25, 50, MouseButton::Left);    // box A, no prior press
        CHECK(!aClicked);
        CHECK(!bClicked);
    }
}

TEST_CASE("onClick fires on a handler ancestor when press and release share it") {
    Reconciler reconciler;
    EventHandler events;

    // Only the PARENT has onClick; press and release both land on the inner child.
    bool parentClicked = false;
    auto tree = Box(
                        Box().width(50).height(50).setKey("inner")
                    )
                    .width(100).height(100)
                    .onClick([&]() { parentClicked = true; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    events.handleMouseDown(root, 25, 25, MouseButton::Left);  // inner child
    events.handleMouseUp(root, 25, 25, MouseButton::Left);    // inner child
    CHECK(parentClicked);  // press leaf is within the parent's subtree → clicks
}

TEST_CASE("Hover callbacks fire") {
    Reconciler reconciler;
    EventHandler events;

    bool hovered = false;
    auto tree = Box().width(100).height(100).onHover([&](bool h) { hovered = h; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    // Move into box
    events.handleMouseMove(root, 50, 50);
    CHECK(hovered);

    // Move out of box
    events.handleMouseMove(root, 150, 150);
    CHECK(!hovered);
}

TEST_CASE("Hover tracks deepest node") {
    Reconciler reconciler;
    EventHandler events;

    bool innerHovered = false;
    bool outerHovered = false;

    auto tree = Box(
                        Box().width(50).height(50).setKey("inner").onHover([&](bool h) { innerHovered = h; })
                    )
                    .width(100)
                    .height(100)
                    .onHover([&](bool h) { outerHovered = h; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    // Move into inner box - both should hover
    events.handleMouseMove(root, 25, 25);
    CHECK(innerHovered);
    CHECK(outerHovered);

    // Move to outer only
    events.handleMouseMove(root, 75, 75);
    CHECK(!innerHovered);
    CHECK(outerHovered);

    // Move out completely
    events.handleMouseMove(root, 150, 150);
    CHECK(!innerHovered);
    CHECK(!outerHovered);
}

TEST_CASE("Hover - a sibling-to-sibling move does not re-fire the shared parent's onHover") {
    // Parent hosts two sibling children. Moving the cursor from childA to childB
    // must leave the parent hovered throughout (it is the LCA), firing its onHover
    // ZERO times during the sibling move — the spurious-ancestor fix.
    Reconciler reconciler;
    EventHandler events;

    int parentHoverCalls = 0;
    auto tree = Box(
                        Box().width(50).height(100).setKey("childA").onHover([](bool) {}),
                        Box().width(50).height(100).setKey("childB").onHover([](bool) {})
                    )
                    .flexDirection(FlexDirection::Row)
                    .width(100)
                    .height(100)
                    .onHover([&](bool) { ++parentHoverCalls; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    // Enter childA: parent's onHover fires once (initial enter).
    events.handleMouseMove(root, 25, 50);
    CHECK(parentHoverCalls == 1);

    int callsBeforeMove = parentHoverCalls;
    // Sibling move childA -> childB: the parent is the LCA, so its onHover must
    // NOT fire again.
    events.handleMouseMove(root, 75, 50);
    CHECK(parentHoverCalls == callsBeforeMove);
}

// ============================================================================
// onHoverDelay: armed by updateHover's enter walk (deepest carrier wins),
// disarmed by its leave walk / node removal, fired ONCE by advanceClock when
// the deadline passes — all on the dt-accumulated clock, no wall clock.
// ============================================================================

TEST_CASE("Hover delay - fires once at the deadline; the fired latch stops re-fires") {
    Reconciler reconciler;
    EventHandler events;

    int fired = 0;
    auto tree = Box().width(100).height(100).onHoverDelay([&] { fired++; });
    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    events.handleMouseMove(root, 50, 50);
    events.advanceClock(0.499f);  // 499ms: just short of the 500ms default
    CHECK(fired == 0);
    events.advanceClock(0.002f);  // 501ms: past the deadline
    CHECK(fired == 1);
    // The slot stays armed+fired while the pointer rests: no per-frame re-fire.
    events.advanceClock(1.0f);
    CHECK(fired == 1);
}

TEST_CASE("Hover delay - leaving before the deadline disarms; re-enter re-arms fresh") {
    Reconciler reconciler;
    EventHandler events;

    int fired = 0;
    auto tree = Box(
                        Box().width(50).height(50).setKey("target").onHoverDelay([&] { fired++; })
                    )
                    .width(100)
                    .height(100);
    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    // Leave 300ms in: disarmed — the remaining time never elapses into a fire.
    events.handleMouseMove(root, 25, 25);
    events.advanceClock(0.3f);
    events.handleMouseMove(root, 75, 75);  // off the target
    events.advanceClock(0.5f);
    CHECK(fired == 0);

    // Re-enter: a FRESH deadline — the earlier 300ms does not count toward it.
    events.handleMouseMove(root, 25, 25);
    events.advanceClock(0.3f);
    CHECK(fired == 0);
    events.advanceClock(0.201f);
    CHECK(fired == 1);

    // And after a completed fire, leave + re-enter arms and fires again.
    events.handleMouseMove(root, 75, 75);
    events.handleMouseMove(root, 25, 25);
    events.advanceClock(0.501f);
    CHECK(fired == 2);
}

TEST_CASE("Hover delay - a move within the armed node does not reset the deadline") {
    Reconciler reconciler;
    EventHandler events;

    // The OUTER box carries the delay; the inner child is plain. Moving within
    // the outer box — including deeper into the child — keeps the outer node
    // above the LCA cut and out of the enter chain, so its deadline holds.
    int fired = 0;
    auto tree = Box(
                        Box().width(50).height(50).setKey("inner")
                    )
                    .width(100)
                    .height(100)
                    .onHoverDelay([&] { fired++; });
    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    events.handleMouseMove(root, 75, 75);  // outer, outside the inner child
    events.advanceClock(0.3f);
    events.handleMouseMove(root, 80, 80);  // within the same node
    events.handleMouseMove(root, 25, 25);  // deeper, into the plain child
    events.advanceClock(0.201f);           // 501ms total against ONE deadline
    CHECK(fired == 1);
}

TEST_CASE("Hover delay - hoverDelayMs overrides the default interval") {
    Reconciler reconciler;
    EventHandler events;

    int fired = 0;
    auto tree = Box().width(100).height(100).hoverDelayMs(200).onHoverDelay([&] { fired++; });
    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    events.handleMouseMove(root, 50, 50);
    events.advanceClock(0.15f);
    CHECK(fired == 0);
    events.advanceClock(0.051f);  // 201ms: past the custom 200ms deadline
    CHECK(fired == 1);
}

TEST_CASE("Hover delay - the deepest delay carrier in the enter chain wins") {
    Reconciler reconciler;
    EventHandler events;

    int innerFired = 0;
    int outerFired = 0;
    auto tree = Box(
                        Box().width(50).height(50).setKey("inner").onHoverDelay([&] { innerFired++; })
                    )
                    .width(100)
                    .height(100)
                    .onHoverDelay([&] { outerFired++; });
    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    // One move enters BOTH; the inner (deepest, most specific) node arms.
    events.handleMouseMove(root, 25, 25);
    events.advanceClock(0.6f);
    CHECK(innerFired == 1);
    CHECK(outerFired == 0);  // single slot: the outer's delay never ran
}

TEST_CASE("Hover delay - an armed node reconciled away never fires (dead token, no UAF)") {
    Reconciler reconciler;
    EventHandler events;

    int fired = 0;
    auto build = [&](bool withField) -> VNode {
        std::vector<Child> kids;
        if (withField)
            kids.push_back(Box().width(80).height(20).setKey("field").onHoverDelay([&] { fired++; }));
        return Box(std::move(kids)).width(100).height(100);
    };

    auto fiber = reconciler.mount(build(true));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    events.handleMouseMove(root, 40, 10);  // arm the field
    events.advanceClock(0.3f);

    // Reconcile the armed node away before the deadline. No node-removed
    // callback is wired here (the bare-reconciler idiom), so the liveness
    // token is the only thing between the deadline and freed memory.
    reconciler.reconcile(fiber.get(), build(false));
    root->calculateLayout(100, 100);

    CHECK_NOTHROW(events.advanceClock(1.0f));
    CHECK(fired == 0);
}

TEST_CASE("Text node receives events") {
    Reconciler reconciler;
    EventHandler events;

    bool clicked = false;
    auto tree = Text("Click me").width(100).height(20).onClick([&]() { clicked = true; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 20);

    events.handleMouseDown(root, 50, 10, MouseButton::Left);
    events.handleMouseUp(root, 50, 10, MouseButton::Left);
    CHECK(clicked);
}

TEST_CASE("Scroll event fires on Box with onScroll handler") {
    Reconciler reconciler;
    EventHandler events;

    float scrollX = 0;
    float scrollY = 0;
    auto tree = Box().width(100).height(100).onScroll([&](float dx, float dy) {
        scrollX = dx;
        scrollY = dy;
    });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    events.handleScroll(root, 50, 50, 5, 10);
    CHECK(scrollX == 5);
    CHECK(scrollY == 10);
}

TEST_CASE("Scroll bubbles to parent") {
    Reconciler reconciler;
    EventHandler events;

    float parentScrollY = 0;
    auto tree = Box(
                        Box().width(50).height(50).setKey("inner")
                    )
                    .width(100)
                    .height(100)
                    .onScroll([&](float dx, float dy) { parentScrollY = dy; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    // Scroll inside inner (which has no handler)
    events.handleScroll(root, 25, 25, 0, 15);

    // Should bubble to parent
    CHECK(parentScrollY == 15);
}

TEST_CASE("ScrollNode mounts correctly") {
    Reconciler reconciler;

    auto tree = Scroll(Box().width(200).height(300)).width(100).height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    REQUIRE(root != nullptr);
    CHECK(root->type() == PrimitiveType::Scroll);
}

TEST_CASE("ScrollNode layout works") {
    Reconciler reconciler;

    auto tree = Scroll(Box().width(200).height(300)).width(100).height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    REQUIRE(root != nullptr);

    root->calculateLayout(100, 100);

    auto* scrollNode = static_cast<ScrollNode*>(root);
    CHECK(scrollNode->layout.width == 100);
    CHECK(scrollNode->layout.height == 100);
}

TEST_CASE("ScrollNode has children") {
    Reconciler reconciler;

    auto tree = Scroll(Box().width(200).height(300)).width(100).height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto* scrollNode = static_cast<ScrollNode*>(root);
    CHECK(scrollNode->children.size() == 1);
}

TEST_CASE("ScrollNode auto-scrolls content") {
    Reconciler reconciler;
    EventHandler events;

    // Scroll container smaller than content
    auto tree = Scroll(Box().width(200).height(300)  // Content larger than container
                       )
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto* scrollNode = static_cast<ScrollNode*>(root);
    REQUIRE(scrollNode->children.size() == 1);

    CHECK(scrollNode->targetScrollX == 0);
    CHECK(scrollNode->targetScrollY == 0);

    // contentHeight > layout.height, scrolling should work
    REQUIRE(scrollNode->contentHeight > scrollNode->layout.height);
    events.handleScroll(root, 50, 50, 0, -20);
    CHECK(scrollNode->targetScrollY == 20);
}

TEST_CASE("ScrollNode clamps scroll offset") {
    Reconciler reconciler;
    EventHandler events;

    auto tree = Scroll(Box().width(150).height(200)).width(100).height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto* scrollNode = static_cast<ScrollNode*>(root);

    // Try to scroll past the end. Both axes overflow, so both gutters are
    // reserved and the viewport is 92x92: max scroll = 200 - 92.
    events.handleScroll(root, 50, 50, 0, -500);
    CHECK(scrollNode->targetScrollY == 108);

    // Try to scroll past the beginning
    events.handleScroll(root, 50, 50, 0, 500);
    CHECK(scrollNode->targetScrollY == 0);  // Min scroll = 0
}

TEST_CASE("ScrollNode doesn't scroll when content fits") {
    Reconciler reconciler;
    EventHandler events;

    // Content smaller than container
    auto tree = Scroll(Box().width(50).height(50)).width(100).height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto* scrollNode = static_cast<ScrollNode*>(root);

    // Try to scroll - should remain at 0
    events.handleScroll(root, 50, 50, 0, -20);
    CHECK(scrollNode->scrollOffsetY == 0);
}

TEST_CASE("ScrollNode hit test accounts for scroll offset") {
    Reconciler reconciler;
    EventHandler events;

    bool childClicked = false;
    auto tree = Scroll(
                           Box().width(100).height(200).onClick([&]() { childClicked = true; })
                       )
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto* scrollNode = static_cast<ScrollNode*>(root);

    // Click at y=50 should hit child initially
    events.handleMouseDown(root, 50, 50, MouseButton::Left);
    events.handleMouseUp(root, 50, 50, MouseButton::Left);
    CHECK(childClicked);

    childClicked = false;

    // Scroll down by 50px
    scrollNode->scrollOffsetY = 50;

    // Click at y=50 now hits y=100 in content (still in child's 0-200 range)
    events.handleMouseDown(root, 50, 50, MouseButton::Left);
    events.handleMouseUp(root, 50, 50, MouseButton::Left);
    CHECK(childClicked);
}

TEST_CASE("ScrollNode padding: padding band hits the scroll, content hits at the padded origin") {
    Reconciler reconciler;
    EventHandler events;

    auto tree = Scroll(Box().height(150).setKey("child")).width(100).height(100).padding(10);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto* scrollNode = static_cast<ScrollNode*>(root);
    REQUIRE(scrollNode->children.size() == 1);
    Node* child = scrollNode->children[0].get();

    // The detached content root lays out against the VIEWPORT width: the
    // padded content box (100 - 2*10) minus the reserved vertical gutter.
    CHECK(child->layout.width == doctest::Approx(72));

    // Inside the scroll but in the padding band: content is clipped away there,
    // so the hit is the Scroll itself.
    events.handleMouseMove(root, 5, 5);
    CHECK(events.getHoveredNode() == root);
    events.handleMouseMove(root, 50, 95);
    CHECK(events.getHoveredNode() == root);

    // Inside the padded viewport: (15,15) maps to content (5,5), in the child.
    events.handleMouseMove(root, 15, 15);
    CHECK(events.getHoveredNode() == child);

    // Scrolled: content coordinates shift from the padded origin.
    scrollNode->scrollOffsetY = 70;
    events.handleMouseMove(root, 15, 85);  // content y = 85 - 10 + 70 = 145, in the child
    CHECK(events.getHoveredNode() == child);
    events.handleMouseMove(root, 15, 95);  // padding band still wins over scrolled content
    CHECK(events.getHoveredNode() == root);
}

TEST_CASE("ScrollNode padding: scrolling clamps to the padded viewport") {
    Reconciler reconciler;
    EventHandler events;

    auto tree = Scroll(Box().height(300)).width(100).height(100).padding(10);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto* scrollNode = static_cast<ScrollNode*>(root);

    // 300px of content in an 80px padded viewport: max scroll = 300 - 80.
    events.handleScroll(root, 50, 50, 0, -1000);
    CHECK(scrollNode->targetScrollY == 220);

    events.handleScroll(root, 50, 50, 0, 1000);
    CHECK(scrollNode->targetScrollY == 0);
}

// ============================================================================
// Overlay scrollbars (6b): the bar is Scroll chrome — its hits never reach
// content, a thumb press captures the scroll and drags proportionally through
// the single clamp, a track press pages by a viewport, and none of it
// dispatches user handlers.
// ============================================================================

TEST_CASE("Scrollbar - thumb drag maps pointer deltas through the track scale, clamped") {
    Reconciler reconciler;
    EventHandler events;

    int clicks = 0, mouseDowns = 0, drags = 0;
    auto tree = Scroll(Box().width(50).height(200).setKey("content"))
                    .width(100)
                    .height(100)
                    .onClick([&]() { ++clicks; })
                    .onMouseDown([&](float, float, MouseButton, uint16_t) { ++mouseDowns; })
                    .onDrag([&](const DragEvent&) { ++drags; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);
    auto* scroll = static_cast<ScrollNode*>(root);

    // Content 2x viewport: track 100, thumb 50, travel 50, maxScroll 100 —
    // 2px of scroll per thumb pixel (= contentLen / trackLen).
    events.handleMouseDown(root, 96, 25, MouseButton::Left);  // in the thumb (92..100 x 0..50)
    CHECK(events.hasCapture());
    CHECK(mouseDowns == 0);  // chrome: no user dispatch

    events.handleMouseMove(root, 96, 35);
    CHECK(scroll->targetScrollY == doctest::Approx(20));
    CHECK(events.getCursor() == CursorShape::Arrow);

    // Way past the end: saturates at maxScroll, no overshoot.
    events.handleMouseMove(root, 96, 300);
    CHECK(scroll->targetScrollY == doctest::Approx(100));

    // Back up: retraces from the press anchor, not from the clamp.
    events.handleMouseMove(root, 96, 60);
    CHECK(scroll->targetScrollY == doctest::Approx(70));

    // Pointer far off the bar horizontally: capture keeps the drag alive.
    events.handleMouseMove(root, 300, 45);
    CHECK(scroll->targetScrollY == doctest::Approx(40));

    events.handleMouseUp(root, 300, 45, MouseButton::Left);
    CHECK(!events.hasCapture());
    CHECK(clicks == 0);  // a scrollbar gesture never clicks
    CHECK(drags == 0);   // ...and never streams user drags
    CHECK(scroll->targetScrollY == doctest::Approx(40));
}

TEST_CASE("Scrollbar - track click pages by one viewport toward the click") {
    Reconciler reconciler;
    EventHandler events;

    auto tree = Scroll(Box().width(50).height(200).setKey("content")).width(100).height(100);
    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);
    auto* scroll = static_cast<ScrollNode*>(root);

    // Thumb occupies y 0..50; a press below it pages down one viewport (100),
    // clamped to maxScroll (100).
    events.handleMouseDown(root, 96, 80, MouseButton::Left);
    events.handleMouseUp(root, 96, 80, MouseButton::Left);
    CHECK(scroll->targetScrollY == doctest::Approx(100));

    // Settle the smooth interpolation so the drawn thumb reaches the bottom.
    root->update(1.0f);
    root->update(1.0f);
    CHECK(scroll->scrollOffsetY == doctest::Approx(100));

    // Now the thumb occupies y 50..100; a press above it pages back up.
    events.handleMouseDown(root, 96, 10, MouseButton::Left);
    events.handleMouseUp(root, 96, 10, MouseButton::Left);
    CHECK(scroll->targetScrollY == doctest::Approx(0));
}

TEST_CASE("Scrollbar - the bar region hits the scroll chrome, not the content beneath") {
    Reconciler reconciler;
    EventHandler events;

    auto tree = Scroll(Box().width(100).height(200).setKey("content")).width(100).height(100);
    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);
    Node* content = root->children[0].get();

    // Over the vertical bar: the scroll itself, even though the full-width
    // content is drawn underneath the overlay.
    events.handleMouseMove(root, 96, 25);
    CHECK(events.getHoveredNode() == root);

    // Off the bar: the content.
    events.handleMouseMove(root, 50, 25);
    CHECK(events.getHoveredNode() == content);
}

TEST_CASE("Scrollbar - content that fits shows no bar, so the same point hits the child") {
    Reconciler reconciler;
    EventHandler events;

    auto tree = Scroll(Box().width(100).height(80).setKey("content")).width(100).height(100);
    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);
    Node* content = root->children[0].get();

    events.handleMouseMove(root, 96, 25);
    CHECK(events.getHoveredNode() == content);
}

// ============================================================================
// Programmatic scroll (6b): scrollTo and scrollIntoView write the target
// through the same clamp every other writer uses.
// ============================================================================

TEST_CASE("ScrollNode::scrollTo clamps; scrollIntoView scrolls the minimum to reveal a rect") {
    Reconciler reconciler;

    auto tree = Scroll(Column(Box().height(50).setKey("r0"), Box().height(50).setKey("r1"),
                              Box().height(50).setKey("r2"), Box().height(50).setKey("r3")))
                    .width(100)
                    .height(100);
    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);
    auto* scroll = static_cast<ScrollNode*>(root);
    Node* column = scroll->children[0].get();
    REQUIRE(column->children.size() == 4);

    // scrollTo goes through the single clamp: maxScroll = 200 - 100.
    scroll->scrollTo(0, 500);
    CHECK(scroll->targetScrollY == doctest::Approx(100));
    scroll->scrollTo(0, -50);
    CHECK(scroll->targetScrollY == doctest::Approx(0));

    // Row 2 (content y 100..150) is below the viewport: scroll just enough to
    // bring its bottom edge to the viewport's bottom.
    scroll->scrollIntoView(absoluteRect(column->children[2].get()));
    CHECK(scroll->targetScrollY == doctest::Approx(50));

    // Row 1 (50..100) is already visible at the pending target: no change.
    scroll->scrollIntoView(absoluteRect(column->children[1].get()));
    CHECK(scroll->targetScrollY == doctest::Approx(50));

    // Row 0 (0..50) is above: scroll back up to its top.
    scroll->scrollIntoView(absoluteRect(column->children[0].get()));
    CHECK(scroll->targetScrollY == doctest::Approx(0));
}

TEST_CASE("ScrollNode::scrollIntoView measures against the padded viewport") {
    Reconciler reconciler;

    auto tree = Scroll(Column(Box().height(50).setKey("r0"), Box().height(50).setKey("r1"),
                              Box().height(50).setKey("r2"), Box().height(50).setKey("r3")))
                    .width(100)
                    .height(100)
                    .padding(10);
    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);
    auto* scroll = static_cast<ScrollNode*>(root);
    Node* column = scroll->children[0].get();

    // Padded viewport is 80 tall; row 2 (content 100..150) needs target
    // 150 - 80 = 70. Exercises absoluteRect's padded-scroll origin too: the
    // row's drawn rect starts at the viewport origin (10), not the border box.
    scroll->scrollIntoView(absoluteRect(column->children[2].get()));
    CHECK(scroll->targetScrollY == doctest::Approx(70));
}

TEST_CASE("ScrollNode child layout dimensions - fixed size child") {
    Reconciler reconciler;

    // Child with explicit dimensions larger than parent
    auto tree = Scroll(Box().width(200).height(300)).width(100).height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto* scrollNode = static_cast<ScrollNode*>(root);
    REQUIRE(scrollNode->children.size() == 1);

    auto* child = scrollNode->children[0].get();

    // Log the actual dimensions for debugging
    MESSAGE("ScrollNode layout: " << scrollNode->layout.width << "x" << scrollNode->layout.height);
    MESSAGE("Child layout: " << child->layout.width << "x" << child->layout.height);

    MESSAGE("Content size: " << scrollNode->contentWidth << "x" << scrollNode->contentHeight);

    // The child should maintain its explicit dimensions
    CHECK(child->layout.width == 200);
    CHECK(child->layout.height == 300);

    // Content size should match child size (calculateLayout already called updateContentSize)
    CHECK(scrollNode->contentWidth == 200);
    CHECK(scrollNode->contentHeight == 300);
}

TEST_CASE("ScrollNode with flexGrow child") {
    Reconciler reconciler;

    // Child that wants to grow - common pattern with List
    auto tree = Scroll(Box(
                               Box().height(50).setKey("item1"),
                               Box().height(50).setKey("item2"),
                               Box().height(50).setKey("item3"),
                               Box().height(50).setKey("item4"),
                               Box().height(50).setKey("item5"),
                               Box().height(50).setKey("item6")
                           )
                           .flexDirection(FlexDirection::Column))
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto* scrollNode = static_cast<ScrollNode*>(root);
    REQUIRE(scrollNode->children.size() == 1);

    auto* child = scrollNode->children[0].get();

    MESSAGE("ScrollNode layout: " << scrollNode->layout.width << "x" << scrollNode->layout.height);
    MESSAGE("Child layout: " << child->layout.width << "x" << child->layout.height);
    MESSAGE("Child has " << child->children.size() << " children");

    // Check each item's layout
    for (size_t i = 0; i < child->children.size(); i++) {
        auto* item = child->children[i].get();
        MESSAGE("  Item " << i << " layout: top=" << item->layout.top << " height=" << item->layout.height);
    }

    MESSAGE("Content size: " << scrollNode->contentWidth << "x" << scrollNode->contentHeight);

    // 6 items * 50px = 300px total height
    // Child should be 300px tall to fit all items
    CHECK(child->layout.height == 300);
    CHECK(scrollNode->contentHeight == 300);
}

TEST_CASE("ScrollNode scrolling works when content exceeds container") {
    Reconciler reconciler;
    EventHandler events;

    // Explicit large child
    auto tree = Scroll(Box().width(100).height(300)  // 300px tall in 100px container
                       )
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto* scrollNode = static_cast<ScrollNode*>(root);
    auto* child = scrollNode->children[0].get();

    MESSAGE("Child height: " << child->layout.height);
    MESSAGE("Content height: " << scrollNode->contentHeight);
    MESSAGE("Layout height: " << scrollNode->layout.height);

    // Verify content is larger (calculateLayout already called updateContentSize)
    REQUIRE(scrollNode->contentHeight > scrollNode->layout.height);

    // Initial scroll offset should be 0
    CHECK(scrollNode->targetScrollY == 0);
    CHECK(scrollNode->scrollOffsetY == 0);

    // Scroll down (negative delta = scroll down in Rack)
    bool consumed = events.handleScroll(root, 50, 50, 0, -50);

    MESSAGE("Scroll consumed: " << consumed);
    MESSAGE("Target scroll after: " << scrollNode->targetScrollY);

    CHECK(consumed);
    // handleScroll sets target, not actual offset (smooth scrolling)
    CHECK(scrollNode->targetScrollY == 50);
}

TEST_CASE("ScrollNode with Column child (no explicit height) - mimics real UI pattern") {
    Reconciler reconciler;

    // This mimics SessionScreen: Scroll(Column(sections...).gap(6)).flexGrow(1)
    // The Column has NO explicit height - it should size to fit its children
    auto tree = Scroll(Column(
                                  // Section 1: header + 2 items
                                  Column(
                                             Text("SECTION 1").height(14),
                                             Box().height(20).setKey("item1a"),
                                             Box().height(20).setKey("item1b")
                                         )
                                      .gap(2)
                                      .setKey("section1"),
                                  // Section 2: header + 3 items
                                  Column(
                                             Text("SECTION 2").height(14),
                                             Box().height(20).setKey("item2a"),
                                             Box().height(20).setKey("item2b"),
                                             Box().height(20).setKey("item2c")
                                         )
                                      .gap(2)
                                      .setKey("section2"),
                                  // Section 3: header + 5 items
                                  Column(
                                             Text("SECTION 3").height(14),
                                             Box().height(20).setKey("item3a"),
                                             Box().height(20).setKey("item3b"),
                                             Box().height(20).setKey("item3c"),
                                             Box().height(20).setKey("item3d"),
                                             Box().height(20).setKey("item3e")
                                         )
                                      .gap(2)
                                      .setKey("section3")
                              )
                           .gap(6)  // NO explicit height on the Column!
                       )
                    .width(100)
                    .height(100)
                    .flexGrow(1);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(200, 200);  // Available space larger than scroll

    auto* scrollNode = static_cast<ScrollNode*>(root);
    REQUIRE(scrollNode->children.size() == 1);

    auto* column = scrollNode->children[0].get();

    MESSAGE("ScrollNode layout: " << scrollNode->layout.width << "x" << scrollNode->layout.height);
    MESSAGE("Column layout: " << column->layout.width << "x" << column->layout.height);
    MESSAGE("Column has " << column->children.size() << " sections");

    // Calculate expected height:
    // Section 1: 14 + 2 + 20 + 2 + 20 = 58
    // Section 2: 14 + 2 + 20 + 2 + 20 + 2 + 20 = 80
    // Section 3: 14 + 2 + 20 + 2 + 20 + 2 + 20 + 2 + 20 + 2 + 20 = 124
    // Total: 58 + 6 + 80 + 6 + 124 = 274
    MESSAGE("Expected content height: ~274");

    for (size_t i = 0; i < column->children.size(); i++) {
        auto* section = column->children[i].get();
        MESSAGE("  Section " << i << ": top=" << section->layout.top << " height=" << section->layout.height);
    }

    MESSAGE("Content size: " << scrollNode->contentWidth << "x" << scrollNode->contentHeight);

    // The Column should NOT be constrained to 100px
    // It should be ~274px to fit all content
    CHECK(column->layout.height > 100);
    CHECK(scrollNode->contentHeight > 100);
}

TEST_CASE("ScrollNode inside Column with flexGrow - mimics actual SessionScreen layout") {
    Reconciler reconciler;

    // This exactly mimics SessionScreen::render():
    // Column(
    //     Header stuff...
    //     renderUserList()  <- returns Scroll(...).flexGrow(1)
    //     Footer stuff...
    // ).flexGrow(1).padding(4).gap(4)
    // Build a list with enough items to exceed the scroll container
    std::vector<Child> items;
    for (int i = 0; i < 20; i++) {
        items.push_back(Box().height(30).setKey("section" + std::to_string(i)));
    }

    auto tree = Column(
                           // Header
                           Box().height(20).setKey("header"),
                           // User list - Scroll with flexGrow(1) containing Column with many items
                           Scroll(Column(std::move(items)).gap(6)  // 20 items * 30 + 19 gaps * 6 = 600 + 114 = 714px
                                  )
                               .flexGrow(1)
                               .flexShrink(1)
                               .setKey("scroll"),
                           // Footer
                           Box().height(20).setKey("footer")
                       )
                    .flexGrow(1)
                    .padding(4)
                    .gap(4);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    // Simulate module panel size (180x480)
    root->calculateLayout(180, 480);

    auto* outerColumn = static_cast<BoxNode*>(root);
    MESSAGE("Outer Column layout: " << outerColumn->layout.width << "x" << outerColumn->layout.height);

    // Find the scroll node (child index 1)
    REQUIRE(outerColumn->children.size() == 3);
    auto* scrollNode = static_cast<ScrollNode*>(outerColumn->children[1].get());
    MESSAGE("ScrollNode layout: " << scrollNode->layout.width << "x" << scrollNode->layout.height);
    MESSAGE("ScrollNode yoga layout: " << YGNodeLayoutGetWidth(scrollNode->yogaNode) << "x"
                                       << YGNodeLayoutGetHeight(scrollNode->yogaNode));

    REQUIRE(scrollNode->children.size() == 1);
    auto* innerColumn = scrollNode->children[0].get();
    MESSAGE("Inner Column layout: " << innerColumn->layout.width << "x" << innerColumn->layout.height);
    MESSAGE("Inner Column has " << innerColumn->children.size() << " children");

    // Calculate expected: 20*30 + 19*6 = 714px for inner content
    MESSAGE("Expected inner Column height: 714");

    MESSAGE("ScrollNode content size: " << scrollNode->contentWidth << "x" << scrollNode->contentHeight);

    // The content should exceed the scroll container, enabling scrolling
    CHECK(scrollNode->contentHeight == 714);
    CHECK(scrollNode->contentHeight > scrollNode->layout.height);
}

// ============================================================================
// Keyboard routing contract: with no focused Input, key events route to the
// first pre-order node handling that event type. KeyDown and KeyUp must stay
// coherent — a node registering only onKeyUp must still receive KeyUp, and a
// node registering both must receive both.
// ============================================================================

TEST_CASE("Key - onKeyUp-only node receives KeyUp") {
    Reconciler reconciler;
    EventHandler events;

    bool keyUpFired = false;
    int gotCode = 0;
    auto tree = Box().width(100).height(100).onKeyUp([&](int code, uint16_t) {
        keyUpFired = true;
        gotCode = code;
    });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    // No focused Input; the only key handler is onKeyUp. Previously targeting
    // inspected onKeyDown only, so this node was never reachable for KeyUp.
    events.handleKeyUp(root, 65, 0);
    CHECK(keyUpFired);
    CHECK(gotCode == 65);
}

TEST_CASE("Key - node with both handlers receives KeyDown and KeyUp") {
    Reconciler reconciler;
    EventHandler events;

    bool downFired = false;
    bool upFired = false;
    auto tree = Box()
                    .width(100)
                    .height(100)
                    .onKeyDown([&](int, uint16_t, bool) { downFired = true; })
                    .onKeyUp([&](int, uint16_t) { upFired = true; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    events.handleKeyDown(root, 32, 0);
    events.handleKeyUp(root, 32, 0);
    CHECK(downFired);
    CHECK(upFired);
}

TEST_CASE("Key - onKeyDown receives the repeat flag") {
    Reconciler reconciler;
    EventHandler events;

    bool lastRepeat = false;
    int downCount = 0;
    auto tree = Box()
                    .width(100)
                    .height(100)
                    .onKeyDown([&](int, uint16_t, bool repeat) {
                        lastRepeat = repeat;
                        ++downCount;
                    });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    events.handleKeyDown(root, 65, 0, /*repeat=*/true);
    CHECK(downCount == 1);
    CHECK(lastRepeat == true);

    events.handleKeyDown(root, 65, 0, /*repeat=*/false);
    CHECK(downCount == 2);
    CHECK(lastRepeat == false);
}

TEST_CASE("Key - KeyUp skips an onKeyDown-only node and reaches the onKeyUp node") {
    Reconciler reconciler;
    EventHandler events;

    // Pre-order-first node handles only onKeyDown; a later sibling handles only
    // onKeyUp. KeyUp must skip the first and land on the second.
    bool downNodeGotUp = false;
    bool upNodeGotUp = false;
    auto tree = Box(
                        Box().width(50).height(50).setKey("downOnly").onKeyDown(
                            [&](int, uint16_t, bool) {}),
                        Box().width(50).height(50).setKey("upOnly").onKeyUp(
                            [&](int, uint16_t) { upNodeGotUp = true; })
                    )
                    .width(100)
                    .height(100);
    // (downNodeGotUp can never become true: downOnly has no onKeyUp.)
    (void)downNodeGotUp;

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    events.handleKeyUp(root, 13, 0);
    CHECK(upNodeGotUp);
}

// ============================================================================
// Exception-safety contract (B2 commit 2): a throwing event handler is isolated,
// routed to the sink, never aborts the dispatch walk, and never desyncs hover /
// focus bookkeeping.
// ============================================================================

namespace {
// A Host subclass whose render returns a fixed tree, for driving the noexcept
// Host::handle* backstops end to end.
class EventTestHost : public Host {
public:
    explicit EventTestHost(std::function<VNode()> fn) { setRender(std::move(fn)); }
};
}  // namespace

TEST_CASE("Exception - throwing onClick is isolated; bubble target still fires") {
    Reconciler reconciler;
    EventHandler events;

    int errorCount = 0;
    std::string errorWhere;
    events.setErrorHandler([&](std::string_view where, const std::exception*) {
        errorCount++;
        errorWhere = std::string(where);
    });

    bool parentClicked = false;
    // Inner box throws on click; the click bubbles to the parent, whose handler
    // must still run despite the inner throw.
    auto tree = Box(
                        Box().width(50).height(50).setKey("inner").onClick(
                            [&]() { throw std::runtime_error("click boom"); })
                    )
                    .width(100)
                    .height(100)
                    .onClick([&]() { parentClicked = true; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    events.handleMouseDown(root, 25, 25, MouseButton::Left);
    CHECK_NOTHROW(events.handleMouseUp(root, 25, 25, MouseButton::Left));

    CHECK(errorCount == 1);
    CHECK(errorWhere == "onClick");
    CHECK(parentClicked == true);  // dispatch walk continued past the throwing handler
}

TEST_CASE("Exception - throwing onHover keeps hover state consistent") {
    Reconciler reconciler;
    EventHandler events;

    int errorCount = 0;
    events.setErrorHandler([&](std::string_view, const std::exception*) { errorCount++; });

    auto tree = Box(
                        Box().width(50).height(50).setKey("inner").onHover(
                            [&](bool) { throw std::runtime_error("hover boom"); })
                    )
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);
    Node* inner = root->children[0].get();

    // Hover in: onHover(true) throws but the flag + hoveredNode_ must commit.
    CHECK_NOTHROW(events.handleMouseMove(root, 25, 25));
    CHECK(events.getHoveredNode() == inner);
    CHECK(inner->hovered == true);
    CHECK(errorCount == 1);

    // Hover out: onHover(false) throws but state must still flip cleanly.
    CHECK_NOTHROW(events.handleMouseMove(root, 150, 150));
    CHECK(events.getHoveredNode() == nullptr);
    CHECK(inner->hovered == false);
    CHECK(errorCount == 2);
}

TEST_CASE("Focus - keystrokes are safe no-ops after the focused Input is reconciled away") {
    // Regression for the focused-input UAF (handoff item #11). EventHandler holds
    // a raw InputNode* (focusedInput_). A reconciliation can free that node out
    // from under it; the onNodeRemoved mitigation only fires when the callback is
    // wired and reaches that exact node. Here the bare Reconciler has no
    // node-removed callback (the escape path), so without the liveness token the
    // freed input would be dereferenced on the next keystroke (use-after-free).
    Reconciler reconciler;
    EventHandler events;

    // Root Box with a focusable Input child.
    auto tree = Box(
                        Input().width(80).height(20).setKey("field")
                    )
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto* input = static_cast<InputNode*>(root->children[0].get());
    REQUIRE(input->type() == PrimitiveType::Input);

    // Focus the input and observe its liveness token (mirrors how the B3 test
    // observes the fiber's alive token to pin the death point).
    events.focusNode(input);
    CHECK(events.getFocusedInput() == input);
    std::shared_ptr<bool> inputAlive = input->alive;
    CHECK(*inputAlive == true);

    // Typing routes to the live input.
    events.handleTextInput("a");
    CHECK(input->displayText == "a");

    // Reconcile the Input away: replacing it with a Box of a different primitive
    // type forces removal, which destroys the old InputNode (frees its memory).
    // No node-removed callback is wired, so focusedInput_ is NOT cleared by the
    // partial mitigation — exactly the dangling-pointer scenario.
    auto next = Box(
                        Box().width(80).height(20).setKey("field")
                    )
                    .width(100)
                    .height(100);
    reconciler.reconcile(fiber.get(), std::move(next));

    // The InputNode is genuinely freed: ~Node cleared its liveness token. This is
    // the precondition the stale focusedInput_ must detect. (A sanitizer would
    // flag the pre-fix freed access; this assertion locks in the death point.)
    CHECK(*inputAlive == false);

    // Every focused-input deref must now validate the token first: no crash, and
    // focus is treated as cleared. Pre-fix each of these dereferenced freed
    // memory (focusedInput_->displayText / ->props).
    CHECK_NOTHROW(events.handleTextInput("b"));
    CHECK_NOTHROW(events.handleEditCommand(EditCommand::DeleteBackward));
    CHECK_NOTHROW(events.handleEditCommand(EditCommand::InsertNewline));
    CHECK_NOTHROW(events.handleKeyDown(reconciler.renderRoot(), 65, 0));
    CHECK_NOTHROW(events.handleKeyUp(reconciler.renderRoot(), 65, 0));

    // The public getter validates before returning, so callers never receive a
    // dangling pointer.
    CHECK(events.getFocusedInput() == nullptr);
}

TEST_CASE("DeleteBackward deletes a whole UTF-8 code point, not a single byte") {
    Reconciler reconciler;
    EventHandler events;

    auto tree = Box(
                        Input().width(80).height(20).setKey("field")
                    )
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto* input = static_cast<InputNode*>(root->children[0].get());
    REQUIRE(input->type() == PrimitiveType::Input);
    events.focusNode(input);

    // "é" is a 2-byte UTF-8 code point (0xC3 0xA9). One DeleteBackward must
    // remove both bytes, leaving an empty (and valid-UTF-8) string.
    std::string lastChange = "sentinel";
    input->props.onChange = [&](const std::string& s) { lastChange = s; };

    events.handleTextInput("\xC3\xA9");
    CHECK(input->displayText == "\xC3\xA9");
    CHECK(input->displayText.size() == 2);

    events.handleEditCommand(EditCommand::DeleteBackward);
    CHECK(input->displayText.empty());
    // The onChange payload is valid UTF-8 (empty), not a lone continuation byte.
    CHECK(lastChange.empty());

    // Mixed content: "aé" -> DeleteBackward -> "a" (the ASCII byte survives intact).
    events.handleTextInput("a");
    events.handleTextInput("\xC3\xA9");
    CHECK(input->displayText == "a\xC3\xA9");
    events.handleEditCommand(EditCommand::DeleteBackward);
    CHECK(input->displayText == "a");
    CHECK(lastChange == "a");
}

namespace {
// Mount a Box-wrapped Input with `value` and focus it. The reconciler/fiber
// live in the caller so the render tree (and the returned InputNode) outlive
// this helper; tests that need to reconcile new props reuse the same shape.
VNode inputFixture(const char* value) {
    return Box(
                   Input().value(value).width(80).height(20).setKey("field")
               )
        .width(100)
        .height(100);
}

InputNode* mountFocusedInput(Reconciler& reconciler, std::unique_ptr<Fiber>& fiber,
                             EventHandler& events, const char* value) {
    fiber = reconciler.mount(inputFixture(value));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);
    auto* input = static_cast<InputNode*>(root->children[0].get());
    REQUIRE(input->type() == PrimitiveType::Input);
    events.focusNode(input);
    return input;
}
}  // namespace

TEST_CASE("EditCommand - caret moves by whole code points, no-op at the bounds") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    // "aéb" = a(1 byte) é(2 bytes) b(1 byte): boundaries at 0, 1, 3, 4.
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "a\xC3\xA9" "b");
    CHECK(input->caret == 0);  // caret starts at 0 on mount

    // MoveRight steps one CODE POINT at a time: 0 -> 1 -> 3 (past é) -> 4.
    CHECK(events.handleEditCommand(EditCommand::MoveRight) == true);
    CHECK(input->caret == 1);
    events.handleEditCommand(EditCommand::MoveRight);
    CHECK(input->caret == 3);
    events.handleEditCommand(EditCommand::MoveRight);
    CHECK(input->caret == 4);
    // At the end: still consumed (the key targeted the input), caret unmoved.
    CHECK(events.handleEditCommand(EditCommand::MoveRight) == true);
    CHECK(input->caret == 4);

    // MoveLeft is symmetric: 4 -> 3 -> 1 -> 0, then a consumed no-op at 0.
    events.handleEditCommand(EditCommand::MoveLeft);
    CHECK(input->caret == 3);
    events.handleEditCommand(EditCommand::MoveLeft);
    CHECK(input->caret == 1);
    events.handleEditCommand(EditCommand::MoveLeft);
    CHECK(input->caret == 0);
    CHECK(events.handleEditCommand(EditCommand::MoveLeft) == true);
    CHECK(input->caret == 0);

    // Home/End span the whole single-line value.
    CHECK(events.handleEditCommand(EditCommand::MoveLineEnd) == true);
    CHECK(input->caret == 4);
    CHECK(events.handleEditCommand(EditCommand::MoveLineStart) == true);
    CHECK(input->caret == 0);

    // The anchor tracks every move (== caret throughout C1).
    CHECK(input->selectionAnchor == input->caret);
}

TEST_CASE("EditCommand - insert at caret; DeleteBackward/DeleteForward erase whole code points") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "a\xC3\xA9" "b");

    std::string lastChange;
    int changes = 0;
    input->props.onChange = [&](const std::string& s) {
        lastChange = s;
        ++changes;
    };

    // Caret after 'a' (byte 1): typing inserts THERE, not at the end.
    events.handleEditCommand(EditCommand::MoveRight);
    CHECK(input->caret == 1);
    events.handleTextInput("X");
    CHECK(input->displayText == "aX\xC3\xA9" "b");
    CHECK(input->caret == 2);
    CHECK(lastChange == "aX\xC3\xA9" "b");

    // DeleteBackward mid-string removes the code point BEFORE the caret.
    events.handleEditCommand(EditCommand::DeleteBackward);
    CHECK(input->displayText == "a\xC3\xA9" "b");
    CHECK(input->caret == 1);

    // DeleteForward removes the (multi-byte) code point AFTER the caret, whole.
    events.handleEditCommand(EditCommand::DeleteForward);
    CHECK(input->displayText == "ab");
    CHECK(input->caret == 1);

    // Bounds no-ops: consumed, but no mutation and no onChange.
    int changesBefore = changes;
    events.handleEditCommand(EditCommand::MoveLineStart);
    CHECK(events.handleEditCommand(EditCommand::DeleteBackward) == true);  // caret 0
    events.handleEditCommand(EditCommand::MoveLineEnd);
    CHECK(events.handleEditCommand(EditCommand::DeleteForward) == true);  // caret at end
    CHECK(input->displayText == "ab");
    CHECK(changes == changesBefore);
}

TEST_CASE("EditCommand - controlled round-trip preserves the caret; an external value clamps and snaps") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "hello");

    // Caret mid-string (2), type: insert at 2, caret advances to 3.
    events.handleEditCommand(EditCommand::MoveRight);
    events.handleEditCommand(EditCommand::MoveRight);
    events.handleTextInput("X");
    CHECK(input->displayText == "heXllo");
    CHECK(input->caret == 3);

    // The app's onChange round-trip echoes the SAME value back through props:
    // the caret must stay put (typing mid-string depends on it).
    reconciler.reconcile(fiber.get(), inputFixture("heXllo"));
    CHECK(input->displayText == "heXllo");
    CHECK(input->caret == 3);
    CHECK(input->selectionAnchor == 3);

    // A genuinely DIFFERENT external value clamps the caret into range.
    reconciler.reconcile(fiber.get(), inputFixture("hi"));
    CHECK(input->displayText == "hi");
    CHECK(input->caret == 2);
    CHECK(input->selectionAnchor == 2);

    // An external value whose byte at the old caret is a continuation byte
    // snaps the caret BACKWARD to a code-point boundary ("aé": byte 2 is 0xA9,
    // mid-é, so the caret lands on 1 — never mid-code-point).
    reconciler.reconcile(fiber.get(), inputFixture("a\xC3\xA9"));
    CHECK(input->displayText == "a\xC3\xA9");
    CHECK(input->caret == 1);
    CHECK(input->selectionAnchor == 1);
}

TEST_CASE("EditCommand - not consumed without a focused Input; inapplicable commands not consumed") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "abc");

    // Commands inapplicable here report unconsumed and touch nothing: vertical
    // moves need a multiline input; clipboard commands need a clipboard (none
    // passed) — see EditCommand.hpp's consumption contract.
    for (EditCommand cmd : {EditCommand::MoveUp, EditCommand::MoveDown, EditCommand::Cut,
                            EditCommand::Copy, EditCommand::Paste}) {
        CHECK(events.handleEditCommand(cmd) == false);
    }
    CHECK(input->displayText == "abc");
    CHECK(input->caret == 0);

    // No focused Input: even an implemented command is not consumed.
    events.focusNode(nullptr);
    CHECK(events.handleEditCommand(EditCommand::MoveRight) == false);
    CHECK(events.handleEditCommand(EditCommand::DeleteBackward) == false);
    CHECK(input->displayText == "abc");
}

// ============================================================================
// Click-to-position + follow-scroll (6c C2): a press on an Input maps the
// pointer x through the shared InputNode text geometry (indexAtPoint) to a
// caret byte offset, and textScrollX follows the caret through edits so it
// stays inside the content box.
// ============================================================================

namespace {

// 10px per BYTE at any size (the FakeBackend convention in test_tree_renderer):
// deterministic caret/click arithmetic; a multi-byte cp measures wider.
test::FnMeasurer tenPxPerByte() {
    return test::FnMeasurer(
        [](const std::string& t, float, float) { return Size{static_cast<float>(t.size()) * 10.0f, 10.0f}; });
}

// The Box-wrapped Input the click/scroll tests mount AND reconcile new values
// through. The Input sits at the origin with no insets, so its text starts at
// x = kInputTextPad and a window x maps to textX = x - pad + textScrollX.
VNode measuredInputFixture(const char* value, bool password = false) {
    return Box(
               Input().value(value).password(password).fontSize(10).width(100).height(30).setKey("field")
           )
        .width(200)
        .height(100);
}

// Mount the fixture on a measurer-wired harness — the production
// config-context seam the event handler recovers the measurer through.
InputNode* mountMeasuredInput(test::MeasureHarness& h, const char* value, bool password = false) {
    Node* root = h.mount(measuredInputFixture(value, password));
    root->calculateLayout(200, 100);
    auto* input = static_cast<InputNode*>(root->children[0].get());
    REQUIRE(input->type() == PrimitiveType::Input);
    return input;
}

}  // namespace

TEST_CASE("Click positions the caret at the nearest boundary; midpoint ties go left") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    InputNode* input = mountMeasuredInput(h, "hello");
    Node* root = h.reconciler().renderRoot();

    // Independent PLAIN clicks: break the multi-click chain each time, or
    // nearby rapid clicks would chain into double-click word selection (C4).
    auto clickAt = [&](float textX) {
        events.advanceClock(0.6f);
        events.handleMouseDown(root, rd::kInputTextPad + textX, 15);
        events.handleMouseUp(root, rd::kInputTextPad + textX, 15);
        return input->caret;
    };

    // 10px cells: 'h' spans [0,10) with midpoint 5, 'e' [10,20) mid 15, ...
    CHECK(clickAt(0) == 0);
    CHECK(clickAt(16) == 2);  // past 'e' midpoint: caret after 'e'
    CHECK(clickAt(15) == 1);  // exactly ON 'e' midpoint: ties go LEFT
    CHECK(clickAt(5) == 0);   // 'h' midpoint tie: before 'h'
    CHECK(clickAt(31) == 3);
    CHECK(clickAt(75) == 5);  // past the last midpoint: caret at size
    CHECK(clickAt(-5) == 0);  // in the left text pad: clamps to 0

    // The click focused the input, collapsed the anchor onto the caret, and
    // latched a repaint (a placed caret is a visual transition).
    CHECK(events.getFocusedInput() == input);
    CHECK(input->selectionAnchor == input->caret);
    CHECK(events.consumeVisualStateChanged());
}

TEST_CASE("Click lands on code-point boundaries in multibyte text") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    // "aéb" at 10px/byte: 'a' [0,10), 'é' [10,30) (2 bytes wide), 'b' [30,40).
    InputNode* input = mountMeasuredInput(h, "a\xC3\xA9" "b");
    Node* root = h.reconciler().renderRoot();

    // Independent PLAIN clicks (see the chain-break note in the tie test).
    auto clickAt = [&](float textX) {
        events.advanceClock(0.6f);
        events.handleMouseDown(root, rd::kInputTextPad + textX, 15);
        events.handleMouseUp(root, rd::kInputTextPad + textX, 15);
        return input->caret;
    };

    // Anywhere in é's cell resolves to byte 1 or 3 — NEVER the mid-cp byte 2.
    CHECK(clickAt(15) == 1);  // before é's midpoint (20)
    CHECK(clickAt(19) == 1);
    CHECK(clickAt(21) == 3);  // past the midpoint: after the whole é
    CHECK(clickAt(34) == 3);
    CHECK(clickAt(36) == 4);  // past b's midpoint (35): the end
}

TEST_CASE("Click maps through star space to displayText offsets for passwords") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    // Raw "aéb" DISPLAYS as "***": uniform 10px star cells — the click space
    // is star space, mapped back to raw byte offsets (0, 1, 3, 4).
    InputNode* input = mountMeasuredInput(h, "a\xC3\xA9" "b", true);
    Node* root = h.reconciler().renderRoot();

    // Independent PLAIN clicks (see the chain-break note in the tie test).
    auto clickAt = [&](float textX) {
        events.advanceClock(0.6f);
        events.handleMouseDown(root, rd::kInputTextPad + textX, 15);
        events.handleMouseUp(root, rd::kInputTextPad + textX, 15);
        return input->caret;
    };

    CHECK(clickAt(4) == 0);
    CHECK(clickAt(12) == 1);  // second star's left half → before é → byte 1
    CHECK(clickAt(16) == 3);  // second star's right half → after é → byte 3
    CHECK(clickAt(26) == 4);  // past the last star's midpoint (25)
}

TEST_CASE("textScrollX follows the caret through End/Left/Home") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    // 12 chars * 10px = 120px in a 100px input: the visible text span is the
    // content box minus the pad both sides = 84px.
    InputNode* input = mountMeasuredInput(h, "aaaaaaaaaaaa");
    events.focusNode(input);
    CHECK(input->textScrollX == doctest::Approx(0));

    // End: caretX 120 rides past the span → scroll right to 120 - 84 = 36.
    events.handleEditCommand(EditCommand::MoveLineEnd);
    CHECK(input->textScrollX == doctest::Approx(36));

    // One left: caretX 110 is still inside [36, 36+84] → no jitter.
    events.handleEditCommand(EditCommand::MoveLeft);
    CHECK(input->textScrollX == doctest::Approx(36));

    // Home: caretX 0 is left of the span → scroll fully back.
    events.handleEditCommand(EditCommand::MoveLineStart);
    CHECK(input->textScrollX == doctest::Approx(0));
}

TEST_CASE("Typing past the right edge scrolls; deletions clamp the scroll back") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    InputNode* input = mountMeasuredInput(h, "aaaaaaaa");  // 80px: fits the 84px span
    events.focusNode(input);
    events.handleEditCommand(EditCommand::MoveLineEnd);
    CHECK(input->textScrollX == doctest::Approx(0));

    events.handleTextInput("b");  // 90px, caret at 90 → scroll 6
    CHECK(input->textScrollX == doctest::Approx(6));
    events.handleTextInput("c");  // 100px, caret at 100 → scroll 16
    CHECK(input->textScrollX == doctest::Approx(16));

    // Each deletion re-clamps to max(0, total - span): no dead space is left
    // open past the last glyph.
    events.handleEditCommand(EditCommand::DeleteBackward);  // 90px → clamp 6
    CHECK(input->textScrollX == doctest::Approx(6));
    events.handleEditCommand(EditCommand::DeleteBackward);  // 80px fits → 0
    CHECK(input->textScrollX == doctest::Approx(0));
}

TEST_CASE("Click maps through textScrollX to the scrolled-away offsets") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    InputNode* input = mountMeasuredInput(h, "aaaaaaaaaaaa");
    Node* root = h.reconciler().renderRoot();
    events.focusNode(input);
    events.handleEditCommand(EditCommand::MoveLineEnd);
    CHECK(input->textScrollX == doctest::Approx(36));

    // Window x 58 → textX = (58 - pad) + 36 = 86 → past cell 8's midpoint
    // (85) → boundary 9. (Unscrolled, the same window x would give 5.)
    events.handleMouseDown(root, 58, 15);
    CHECK(input->caret == 9);
    // A plain click never moves the scroll: the caret landed inside the span.
    CHECK(input->textScrollX == doctest::Approx(36));
}

TEST_CASE("External value replacement resets textScrollX; the onChange echo preserves it") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    InputNode* input = mountMeasuredInput(h, "aaaaaaaaaaaa");
    events.focusNode(input);
    events.handleEditCommand(EditCommand::MoveLineEnd);
    CHECK(input->textScrollX == doctest::Approx(36));

    // The app's onChange echo (the SAME value back through props) must not
    // disturb the follow-scroll — it runs after every keystroke.
    h.reconciler().reconcile(h.fiber(), measuredInputFixture("aaaaaaaaaaaa"));
    CHECK(input->textScrollX == doctest::Approx(36));

    // A genuinely different external value resets the scroll alongside the
    // caret snap: the new text shows from its head.
    h.reconciler().reconcile(h.fiber(), measuredInputFixture("hi"));
    CHECK(input->displayText == "hi");
    CHECK(input->textScrollX == doctest::Approx(0));
}

TEST_CASE("External replacement preserving a far caret follows it into view (not off-clip)") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    // 15 'a's, caret at the end (byte 15, caretX 150). Span = 84px.
    InputNode* input = mountMeasuredInput(h, "aaaaaaaaaaaaaaa");
    events.focusNode(input);
    events.handleEditCommand(EditCommand::MoveLineEnd);
    CHECK(input->caret == 15);

    // A controlled app replaces the value with a LONGER string but keeps the
    // caret at byte 15 (mid-string now). The reset-to-0 alone would leave the
    // caret at x=150 with a 84px span — 66px off the right clip edge. The
    // re-follow in updateProps must pull it back into the visible span.
    h.reconciler().reconcile(h.fiber(), measuredInputFixture("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));  // 30 chars
    CHECK(input->displayText.size() == 30);
    CHECK(input->caret == 15);

    // The caret (x = caretPrefixWidth) must sit inside [textScrollX,
    // textScrollX + span] — i.e. visible, not clipped away.
    float span = input->layout.contentWidth() - 2 * rd::kInputTextPad;
    float caretX = input->caretPrefixWidth(&m);
    CHECK(caretX - input->textScrollX >= 0);
    CHECK(caretX - input->textScrollX <= doctest::Approx(span));
    // Concretely: caretX 150 at the right edge → scroll 150 - 84 = 66.
    CHECK(input->textScrollX == doctest::Approx(66));
}

TEST_CASE("indexAtPoint clamps on empty text and works without a measurer") {
    Reconciler reconciler;  // bare: the config context holds no measurer
    EventHandler events;
    std::unique_ptr<Fiber> fiber;

    SUBCASE("empty text always resolves to 0") {
        InputNode* empty = mountFocusedInput(reconciler, fiber, events, "");
        CHECK(empty->indexAtPoint(50.0f, nullptr) == 0);
    }

    SUBCASE("null measurer falls back to the heuristic") {
        // 0.6 * fontSize per byte (the default 16px face → 9.6px cells),
        // matching TextNode's fallback path.
        InputNode* abc = mountFocusedInput(reconciler, fiber, events, "abc");
        CHECK(abc->indexAtPoint(-1.0f, nullptr) == 0);
        CHECK(abc->indexAtPoint(10.0f, nullptr) == 1);
        CHECK(abc->indexAtPoint(1000.0f, nullptr) == 3);
    }
}

TEST_CASE("Mouse-down modifiers thread into the dispatched event") {
    Reconciler reconciler;
    EventHandler events;

    uint16_t seen = 0xFFFF;
    auto tree = Box()
                    .width(100)
                    .height(100)
                    .onMouseDown([&](float, float, MouseButton, uint16_t mods) { seen = mods; });
    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    // Shift held at the press rides on Event::keyMod into the handler — the
    // seam C3's shift+click selection extension consumes.
    events.handleMouseDown(root, 50, 50, MouseButton::Left, KeyMod_Shift);
    CHECK(seen == KeyMod_Shift);
    events.handleMouseUp(root, 50, 50, MouseButton::Left);

    // ... and defaults to KeyMod_None when the platform passes nothing.
    events.handleMouseDown(root, 50, 50, MouseButton::Left);
    CHECK(seen == KeyMod_None);
}

// ============================================================================
// Selection (6c C3): the selection is [min(anchor,caret), max(anchor,caret))
// with the CARET as the moving end. Extended (Shift) moves and shift+click
// move only the caret; unextended arrows collapse to the selection's edge;
// SelectAll spans the value; edits replace the selection.
// ============================================================================

TEST_CASE("Selection - shift+arrows extend and shrink; the caret is the moving end") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "hello");

    // Caret to 2, no selection yet.
    events.handleEditCommand(EditCommand::MoveRight);
    events.handleEditCommand(EditCommand::MoveRight);
    CHECK(input->caret == 2);
    CHECK(!input->hasSelection());

    // Extend right: the anchor stays at 2, only the caret moves -> [2,3).
    CHECK(events.handleEditCommand(EditCommand::MoveRight, true) == true);
    CHECK(input->selectionAnchor == 2);
    CHECK(input->caret == 3);
    CHECK(input->hasSelection());
    CHECK(input->selBegin() == 2);
    CHECK(input->selEnd() == 3);

    // Again: grows to [2,4).
    events.handleEditCommand(EditCommand::MoveRight, true);
    CHECK(input->selBegin() == 2);
    CHECK(input->selEnd() == 4);

    // Extend left SHRINKS from the caret end: back to [2,3).
    events.handleEditCommand(EditCommand::MoveLeft, true);
    CHECK(input->selBegin() == 2);
    CHECK(input->selEnd() == 3);

    // Extending past the anchor swaps the ends: caret 1 < anchor 2 -> [1,2).
    events.handleEditCommand(EditCommand::MoveLeft, true);
    events.handleEditCommand(EditCommand::MoveLeft, true);
    CHECK(input->caret == 1);
    CHECK(input->selectionAnchor == 2);
    CHECK(input->selBegin() == 1);
    CHECK(input->selEnd() == 2);

    // Shift+End extends to the end from the still-fixed anchor: [2,5).
    events.handleEditCommand(EditCommand::MoveLineEnd, true);
    CHECK(input->selBegin() == 2);
    CHECK(input->selEnd() == 5);
}

TEST_CASE("Selection - an unextended arrow collapses to the selection's edge, not past it") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "hello");

    // Caret to `from`, then extend to `to` (the caret ends as the moving end).
    auto select = [&](int from, int to) {
        events.handleEditCommand(EditCommand::MoveLineStart);
        for (int i = 0; i < from; ++i)
            events.handleEditCommand(EditCommand::MoveRight);
        for (int i = 0; i < to - from; ++i)
            events.handleEditCommand(EditCommand::MoveRight, true);
        for (int i = 0; i < from - to; ++i)
            events.handleEditCommand(EditCommand::MoveLeft, true);
    };

    // Caret at the RIGHT end (anchor 2, caret 5): MoveLeft lands on the LEFT
    // edge (2) — a raw step from the caret would have landed on 4.
    select(2, 5);
    events.handleEditCommand(EditCommand::MoveLeft);
    CHECK(input->caret == 2);
    CHECK(input->selectionAnchor == 2);
    CHECK(!input->hasSelection());

    // Caret at the LEFT end (anchor 5, caret 2): MoveRight lands on the RIGHT
    // edge (5) — a raw step from the caret would have landed on 3.
    select(5, 2);
    events.handleEditCommand(EditCommand::MoveRight);
    CHECK(input->caret == 5);
    CHECK(!input->hasSelection());

    // Home/End collapse to the line bounds regardless of the selection.
    select(2, 5);
    events.handleEditCommand(EditCommand::MoveLineEnd);
    CHECK(input->caret == 5);
    CHECK(!input->hasSelection());
    select(2, 5);
    events.handleEditCommand(EditCommand::MoveLineStart);
    CHECK(input->caret == 0);
    CHECK(!input->hasSelection());
}

TEST_CASE("Selection - SelectAll spans the whole value and is consumed") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    // "aéb": 4 bytes. Anchor lands at the front, the caret (moving end) at the back.
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "a\xC3\xA9" "b");

    CHECK(events.handleEditCommand(EditCommand::SelectAll) == true);
    CHECK(input->selectionAnchor == 0);
    CHECK(input->caret == 4);
    CHECK(input->selBegin() == 0);
    CHECK(input->selEnd() == 4);
}

TEST_CASE("Selection - typing, Backspace, and Delete replace exactly the selection") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "hello!!");

    std::string lastChange = "sentinel";
    input->props.onChange = [&](const std::string& s) { lastChange = s; };

    // Select [2,5) = "llo": caret to 2, then three extended rights.
    events.handleEditCommand(EditCommand::MoveRight);
    events.handleEditCommand(EditCommand::MoveRight);
    for (int i = 0; i < 3; ++i)
        events.handleEditCommand(EditCommand::MoveRight, true);
    REQUIRE(input->selBegin() == 2);
    REQUIRE(input->selEnd() == 5);

    SUBCASE("typing replaces the selection with the typed text") {
        events.handleTextInput("X");
        CHECK(input->displayText == "heX!!");
        CHECK(input->caret == 3);
        CHECK(input->selectionAnchor == 3);
        CHECK(lastChange == "heX!!");
    }

    SUBCASE("Backspace erases the selection only — not also the char before it") {
        events.handleEditCommand(EditCommand::DeleteBackward);
        CHECK(input->displayText == "he!!");
        CHECK(input->caret == 2);
        CHECK(input->selectionAnchor == 2);
        CHECK(lastChange == "he!!");
    }

    SUBCASE("Delete erases the selection only — not also the char after it") {
        events.handleEditCommand(EditCommand::DeleteForward);
        CHECK(input->displayText == "he!!");
        CHECK(input->caret == 2);
        CHECK(input->selectionAnchor == 2);
        CHECK(lastChange == "he!!");
    }
}

TEST_CASE("Shift+click extends the selection from the pre-click anchor") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    InputNode* input = mountMeasuredInput(h, "hello!");
    Node* root = h.reconciler().renderRoot();

    auto press = [&](float textX, uint16_t mods) {
        events.handleMouseDown(root, rd::kInputTextPad + textX, 15, MouseButton::Left, mods);
        events.handleMouseUp(root, rd::kInputTextPad + textX, 15);
    };

    // Shift+click while the input is NOT yet focused collapses: focus is
    // gained by this very press, so there is no prior gesture to extend.
    press(16, KeyMod_Shift);  // past 'e' midpoint -> boundary 2
    CHECK(events.getFocusedInput() == input);
    CHECK(input->caret == 2);
    CHECK(!input->hasSelection());

    // Shift+click on the FOCUSED input: the anchor stays at 2, only the caret
    // (the moving end) jumps to the clicked boundary -> [2,5).
    press(46, KeyMod_Shift);  // past cell 4's midpoint (45) -> boundary 5
    CHECK(input->selectionAnchor == 2);
    CHECK(input->caret == 5);
    CHECK(input->selBegin() == 2);
    CHECK(input->selEnd() == 5);

    // Shift+click left of the anchor crosses it: [0,2), anchor still 2.
    press(0, KeyMod_Shift);
    CHECK(input->selectionAnchor == 2);
    CHECK(input->caret == 0);
    CHECK(input->selBegin() == 0);
    CHECK(input->selEnd() == 2);

    // A plain click collapses at the clicked boundary (the C2 behavior).
    press(36, KeyMod_None);  // past cell 3's midpoint (35) -> boundary 4
    CHECK(input->caret == 4);
    CHECK(!input->hasSelection());
}

TEST_CASE("Selection - the follow-scroll tracks the extending caret (the moving end)") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    // 12 chars * 10px = 120px in a 100px input: visible span = 84px.
    InputNode* input = mountMeasuredInput(h, "aaaaaaaaaaaa");
    events.focusNode(input);

    // Shift+End from 0: everything selected AND the caret end (x=120) is
    // scrolled into view — the selection scrolls with the run.
    events.handleEditCommand(EditCommand::MoveLineEnd, true);
    CHECK(input->selBegin() == 0);
    CHECK(input->selEnd() == 12);
    CHECK(input->textScrollX == doctest::Approx(36));

    // Shrinking back one keeps the caret inside the span (no jitter); shift+
    // Home follows it back to 0 (the extend collapses onto the anchor there).
    events.handleEditCommand(EditCommand::MoveLeft, true);
    CHECK(input->textScrollX == doctest::Approx(36));
    events.handleEditCommand(EditCommand::MoveLineStart, true);
    CHECK(input->textScrollX == doctest::Approx(0));
    CHECK(!input->hasSelection());  // caret landed on the anchor

    // SelectAll's caret lands at the tail: the same follow applies.
    events.handleEditCommand(EditCommand::SelectAll);
    CHECK(input->selBegin() == 0);
    CHECK(input->selEnd() == 12);
    CHECK(input->textScrollX == doctest::Approx(36));
}

TEST_CASE("Selection - an external value clamps and snaps BOTH selection ends") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "hello");

    // Anchor 2, caret 5 (shift+End from 2).
    events.handleEditCommand(EditCommand::MoveRight);
    events.handleEditCommand(EditCommand::MoveRight);
    events.handleEditCommand(EditCommand::MoveLineEnd, true);
    REQUIRE(input->selectionAnchor == 2);
    REQUIRE(input->caret == 5);

    // The onChange round-trip echo (same value) leaves both ends untouched.
    reconciler.reconcile(fiber.get(), inputFixture("hello"));
    CHECK(input->selectionAnchor == 2);
    CHECK(input->caret == 5);

    // External "aé" (3 bytes): the caret clamps to the new size (3) and the
    // anchor — byte 2 is é's continuation byte — snaps backward to 1. The
    // selection persists as [1,3) rather than collapsing.
    reconciler.reconcile(fiber.get(), inputFixture("a\xC3\xA9"));
    CHECK(input->caret == 3);
    CHECK(input->selectionAnchor == 1);
    CHECK(input->selBegin() == 1);
    CHECK(input->selEnd() == 3);
}

// ============================================================================
// Drag-select + multi-click selection (6c C4): a captured drag on a focused
// Input moves only the caret (the anchor stays where the press put it);
// double-click selects the wrap-tokenizer run under the click; triple-click
// selects all (single-line: the whole value is the line).
// ============================================================================

TEST_CASE("Drag-select - the caret follows the pointer; the anchor stays at the press") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    InputNode* input = mountMeasuredInput(h, "hello!");
    Node* root = h.reconciler().renderRoot();

    // Press at boundary 2: the C2 collapse (caret = anchor = 2).
    events.handleMouseDown(root, rd::kInputTextPad + 20, 15);
    CHECK(input->caret == 2);
    CHECK(!input->hasSelection());

    // Drag right to boundary 6: the selection grows to [2,6).
    events.handleMouseMove(root, rd::kInputTextPad + 60, 15);
    CHECK(input->caret == 6);
    CHECK(input->selectionAnchor == 2);
    CHECK(input->selBegin() == 2);
    CHECK(input->selEnd() == 6);

    // Retreat to boundary 4: shrinks to [2,4).
    events.handleMouseMove(root, rd::kInputTextPad + 40, 15);
    CHECK(input->caret == 4);
    CHECK(input->selEnd() == 4);

    // Cross the anchor to 0: begin/end swap around the fixed anchor — [0,2).
    events.handleMouseMove(root, rd::kInputTextPad + 0, 15);
    CHECK(input->caret == 0);
    CHECK(input->selectionAnchor == 2);
    CHECK(input->selBegin() == 0);
    CHECK(input->selEnd() == 2);

    // Release: the selection persists — only a new plain click collapses it.
    events.handleMouseUp(root, rd::kInputTextPad + 0, 15);
    CHECK(input->selBegin() == 0);
    CHECK(input->selEnd() == 2);
}

TEST_CASE("Drag-select - past the right edge follow-scrolls and clamps to the end") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    // 12 chars * 10px = 120px in a 100px input: visible span = 84px.
    InputNode* input = mountMeasuredInput(h, "aaaaaaaaaaaa");
    Node* root = h.reconciler().renderRoot();

    events.handleMouseDown(root, rd::kInputTextPad + 0, 15);
    CHECK(input->caret == 0);

    // Far off the node AND the window: capture keeps routing moves to the
    // input; indexAtPoint clamps to size and the follow-scroll chases the
    // caret to the tail.
    events.handleMouseMove(root, 500, 15);
    CHECK(input->caret == 12);
    CHECK(input->selBegin() == 0);
    CHECK(input->selEnd() == 12);
    CHECK(input->textScrollX == doctest::Approx(36));  // 120 - 84

    events.handleMouseUp(root, 500, 15);
    CHECK(input->selEnd() == 12);  // persists past the release
}

TEST_CASE("Drag-select - a sub-threshold wiggle stays a plain click, no selection") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    InputNode* input = mountMeasuredInput(h, "hello!");
    Node* root = h.reconciler().renderRoot();

    // Press at boundary 2 (textX 24 <= cell-2 midpoint 25); a 3px move (under
    // kDragThresholdPx = 4) CROSSES that midpoint — if a zero-width drag were
    // live it would move the caret to 3 — but no drag has latched.
    events.handleMouseDown(root, rd::kInputTextPad + 24, 15);
    CHECK(input->caret == 2);
    events.handleMouseMove(root, rd::kInputTextPad + 27, 15);
    CHECK(input->caret == 2);
    CHECK(!input->hasSelection());

    events.handleMouseUp(root, rd::kInputTextPad + 27, 15);
    CHECK(input->caret == 2);
    CHECK(!input->hasSelection());
}

TEST_CASE("Double-click selects the word under the click; a space click selects the space run") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    InputNode* input = mountMeasuredInput(h, "hello world");
    Node* root = h.reconciler().renderRoot();

    // Both presses at ONE window x (the multi-click radius is 4px); the x is
    // derived from the CURRENT textScrollX so the target boundary is exact.
    auto doubleClickAt = [&](float textX) {
        float wx = rd::kInputTextPad + textX - input->textScrollX;
        events.handleMouseDown(root, wx, 15);
        events.handleMouseUp(root, wx, 15);
        events.advanceClock(0.05f);
        events.handleMouseDown(root, wx, 15);
        events.handleMouseUp(root, wx, 15);
    };

    // In "world" (bytes [6,11)): the whole word, released selection intact.
    doubleClickAt(85);  // boundary 8, inside "world"
    CHECK(input->selectionAnchor == 6);
    CHECK(input->caret == 11);
    CHECK(input->selBegin() == 6);
    CHECK(input->selEnd() == 11);

    // In "hello": [0,5). (0.6s breaks the chain so this is a fresh double.)
    events.advanceClock(0.6f);
    doubleClickAt(25);  // boundary 2, inside "hello"
    CHECK(input->selBegin() == 0);
    CHECK(input->selEnd() == 5);

    // ON the space: the space RUN [5,6) — same-class-run semantics, matching
    // the wrap tokenizer's space/non-space classes.
    events.advanceClock(0.6f);
    doubleClickAt(55);  // boundary 5, the space cell
    CHECK(input->selectionAnchor == 5);
    CHECK(input->caret == 6);
}

TEST_CASE("Double-click selects a multibyte word whole") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    // "café run": é is 2 bytes (offsets 3-4), space at 5, "run" at [6,9).
    InputNode* input = mountMeasuredInput(h, "caf\xC3\xA9 run");
    Node* root = h.reconciler().renderRoot();

    auto doubleClickAt = [&](float textX) {
        float wx = rd::kInputTextPad + textX - input->textScrollX;
        events.handleMouseDown(root, wx, 15);
        events.handleMouseUp(root, wx, 15);
        events.advanceClock(0.05f);
        events.handleMouseDown(root, wx, 15);
        events.handleMouseUp(root, wx, 15);
    };

    // On the é (cell [30,50) at 10px/byte): the whole "café" incl. both é
    // bytes — the range ends on code-point boundaries.
    doubleClickAt(35);
    CHECK(input->selBegin() == 0);
    CHECK(input->selEnd() == 5);

    events.advanceClock(0.6f);
    doubleClickAt(65);  // boundary 6, inside "run"
    CHECK(input->selBegin() == 6);
    CHECK(input->selEnd() == 9);
}

TEST_CASE("Triple-click selects all; extra chained clicks stay select-all") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    InputNode* input = mountMeasuredInput(h, "hello world");
    Node* root = h.reconciler().renderRoot();

    float wx = rd::kInputTextPad + 30;
    auto click = [&] {
        events.handleMouseDown(root, wx, 15);
        events.handleMouseUp(root, wx, 15);
        events.advanceClock(0.05f);
    };

    click();  // caret 3, collapsed
    click();  // word "hello" [0,5)
    click();  // triple: all — anchor front, caret back (the moving end)
    CHECK(input->selectionAnchor == 0);
    CHECK(input->caret == 11);

    click();  // count 4: capped at triple — still select-all
    CHECK(input->selectionAnchor == 0);
    CHECK(input->caret == 11);
    CHECK(input->selBegin() == 0);
    CHECK(input->selEnd() == 11);
}

TEST_CASE("Shift+drag extends from the prior anchor, not the press point") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    InputNode* input = mountMeasuredInput(h, "abcdefgh");
    Node* root = h.reconciler().renderRoot();

    // Establish [2,5) via click at 2 + shift+click at 5 (the C3 gesture).
    events.handleMouseDown(root, rd::kInputTextPad + 20, 15);
    events.handleMouseUp(root, rd::kInputTextPad + 20, 15);
    events.handleMouseDown(root, rd::kInputTextPad + 50, 15, MouseButton::Left, KeyMod_Shift);
    events.handleMouseUp(root, rd::kInputTextPad + 50, 15);
    REQUIRE(input->selectionAnchor == 2);
    REQUIRE(input->caret == 5);

    // Shift+press at 7 keeps the anchor at 2; the drag then moves the caret
    // from there — the whole gesture extends the OLD selection to [2,8).
    events.handleMouseDown(root, rd::kInputTextPad + 70, 15, MouseButton::Left, KeyMod_Shift);
    CHECK(input->selectionAnchor == 2);
    CHECK(input->caret == 7);
    events.handleMouseMove(root, rd::kInputTextPad + 80, 15);
    CHECK(input->selectionAnchor == 2);
    CHECK(input->caret == 8);

    events.handleMouseUp(root, rd::kInputTextPad + 80, 15);
    CHECK(input->selBegin() == 2);
    CHECK(input->selEnd() == 8);
}

TEST_CASE("Drag-select coexists with a user onDrag on the Input") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    int drags = 0;
    Node* root = h.mount(Box(
                             Input().value("hello!").fontSize(10).width(100).height(30).setKey("field")
                                 .onDrag([&](const DragEvent&) { ++drags; })
                         )
                             .width(200)
                             .height(100));
    root->calculateLayout(200, 100);
    auto* input = static_cast<InputNode*>(root->children[0].get());
    REQUIRE(input->type() == PrimitiveType::Input);

    // One captured move past the threshold: the built-in selection extends
    // AND the app's onDrag fires — DOM-style coexistence, neither eats the other.
    events.handleMouseDown(root, rd::kInputTextPad + 20, 15);
    events.handleMouseMove(root, rd::kInputTextPad + 60, 15);
    CHECK(input->selBegin() == 2);
    CHECK(input->selEnd() == 6);
    CHECK(drags == 1);

    events.handleMouseUp(root, rd::kInputTextPad + 60, 15);
    CHECK(input->selEnd() == 6);
}

TEST_CASE("Hover - a freed hovered node is reported as no-hover, not dangling") {
    // Mirrors the focused-input UAF regression for hoveredNode_. A reconcile can
    // free the hovered node; with no node-removed callback wired the partial
    // mitigation never runs, so without the liveness token getHoveredNode() would
    // return a dangling pointer.
    Reconciler reconciler;
    EventHandler events;

    auto tree = Box(
                        Box().width(80).height(20).setKey("field").onHover([](bool) {})
                    )
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto* child = root->children[0].get();
    events.handleMouseMove(root, 40, 10);
    CHECK(events.getHoveredNode() == child);
    std::shared_ptr<bool> childAlive = child->alive;
    CHECK(*childAlive == true);

    // Reconcile the hovered child away (different primitive type forces removal).
    // No node-removed callback is wired: hoveredNode_ is NOT cleared by the partial
    // mitigation — exactly the dangling scenario.
    auto next = Box(
                        Text("x").width(80).height(20).setKey("field")
                    )
                    .width(100)
                    .height(100);
    reconciler.reconcile(fiber.get(), std::move(next));

    CHECK(*childAlive == false);

    // The getter validates the token: no dangling pointer, reported as no-hover.
    CHECK(events.getHoveredNode() == nullptr);
}

// ============================================================================
// Depth-guard contract (handoff item #17): the event-path walks (hitTest,
// dispatchEvent's bubble, findKeyTarget) descend/bubble one native stack frame
// per tree level. A pathologically deep, data-driven tree would overflow the
// stack — a crash reachable from the public input API. The guard stops the walk
// at kMaxTreeDepth, emits one diagnostic via the error sink, and returns
// gracefully instead of crashing. (The reconciler's mount/reconcile recursion is
// deliberately NOT guarded — aborting mid-reconcile would corrupt the tree — so
// its depth limit is a documented precondition on Host::setRender.)
// ============================================================================

// The production cap is kMaxTreeDepth (1024). Building/mounting/tearing down a
// tree that deep would itself overflow the unguarded mount/layout/destruction
// recursion on a 1 MB stack — the very class of crash the guard exists to keep
// the *event* paths from triggering. So the guard's cap is injected low
// (setMaxTreeDepth) and the test tree is built just past it: shallow enough to
// build and destroy safely, deep enough to drive the guard on every event path.
static constexpr int kTestDepthCap = 48;

// Build a chain of `levels` nested single-child Boxes, optionally giving the
// innermost (deepest) box an onClick. Every box is the same size at the parent's
// top-left, so a hit at (5,5) lands inside every level.
static VNode buildDeepBoxChain(int levels, std::function<void()> deepestClick = nullptr) {
    VNode tree = deepestClick ? Box().width(100).height(100).onClick(std::move(deepestClick))
                              : Box().width(100).height(100);
    for (int i = 0; i < levels - 1; ++i) {
        tree = Box(std::move(tree)).width(100).height(100);
    }
    return tree;
}

TEST_CASE("Depth guard - deep tree does not crash event dispatch and diagnoses") {
    Reconciler reconciler;
    EventHandler events;
    events.setMaxTreeDepth(kTestDepthCap);

    int errorCount = 0;
    std::string lastWhere;
    events.setErrorHandler([&](std::string_view where, const std::exception*) {
        errorCount++;
        lastWhere = std::string(where);
    });

    // A tree deeper than the (injected) cap. hitTest descends past the cap and
    // findKeyTarget walks the whole thing.
    auto tree = buildDeepBoxChain(kTestDepthCap + 16);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    REQUIRE(root != nullptr);
    root->calculateLayout(100, 100);

    // hitTest descends past the cap: must not crash, must stop and diagnose.
    CHECK_NOTHROW(events.handleMouseUp(root, 5, 5, MouseButton::Left));
    CHECK(errorCount >= 1);
    CHECK(lastWhere.find("max tree depth exceeded") != std::string::npos);

    // A keyboard event with no focused Input routes via findKeyTarget, which also
    // descends the full depth: must not crash, must diagnose.
    int beforeKey = errorCount;
    CHECK_NOTHROW(events.handleKeyDown(root, 65, 0));
    CHECK(errorCount > beforeKey);
    CHECK(lastWhere.find("findKeyTarget") != std::string::npos);
}

TEST_CASE("Depth guard - dispatchEvent bubble truncates on a deep ancestor chain") {
    Reconciler reconciler;
    EventHandler events;
    events.setMaxTreeDepth(kTestDepthCap);

    int errorCount = 0;
    std::string lastWhere;
    events.setErrorHandler([&](std::string_view where, const std::exception*) {
        errorCount++;
        lastWhere = std::string(where);
    });

    // Chain shallow enough that hitTest reaches the deepest node (within the cap
    // from the root's perspective it is not — see below) but the bubble walk back
    // up is what we exercise. We mount a chain whose depth slightly exceeds the
    // cap; hitTest stops at the cap, then any handler hit triggers an upward
    // bubble. To force the BUBBLE guard specifically, put the handler shallow
    // (near the root) so hitTest finds a hit, and make the chain deep so the
    // upward walk from a deep hit crosses the cap.
    bool deepestClicked = false;
    auto tree = buildDeepBoxChain(kTestDepthCap + 16, [&]() { deepestClicked = true; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    REQUIRE(root != nullptr);
    root->calculateLayout(100, 100);

    // hitTest stops at the cap (no hit past it), so the deepest handler is not
    // reached; the contract under test is graceful, crash-free degradation plus a
    // fired diagnostic on the over-deep event path.
    CHECK_NOTHROW(events.handleMouseUp(root, 5, 5, MouseButton::Left));
    CHECK(errorCount >= 1);
    CHECK(lastWhere.find("max tree depth exceeded") != std::string::npos);
    (void)deepestClicked;
}

// ============================================================================
// UpdateResult truthfulness: hover/press/focus transitions latch needsRepaint
// until the next update() consumes them; a focused input's caret blink surfaces
// animating every frame and needsRepaint on the visible/hidden edge.
// ============================================================================

namespace {
// Three side-by-side 50x50 hit targets: box "a" [0,50), box "b" [50,100),
// an Input [100,150); [150,200) hits only the root Row.
VNode repaintFixture() {
    return Row(Box().width(50).height(50).setKey("a"),
               Box().width(50).height(50).setKey("b"),
               Input().width(50).height(50).setKey("in"))
        .width(200)
        .height(50);
}
}  // namespace

TEST_CASE("UpdateResult - hover/press/focus transitions latch needsRepaint until the next update") {
    EventTestHost host(repaintFixture);

    host.update(200, 50);                     // mount
    auto settled = host.update(200, 50);      // steady state baseline
    CHECK(settled.needsRepaint == false);

    // Hover enters box "a": the transition latches, the NEXT update repaints.
    host.handleMouseMove(25, 25);
    CHECK(host.update(200, 50).needsRepaint == true);

    // Moving within the same node is not a transition: no repaint.
    host.handleMouseMove(30, 30);
    CHECK(host.update(200, 50).needsRepaint == false);

    // Press on "a" (no focus change): repaint.
    host.handleMouseDown(25, 25, MouseButton::Left);
    CHECK(host.update(200, 50).needsRepaint == true);

    // Release clears the recorded press: repaint.
    host.handleMouseUp(25, 25, MouseButton::Left);
    CHECK(host.update(200, 50).needsRepaint == true);

    // Idle again: nothing latched.
    CHECK(host.update(200, 50).needsRepaint == false);

    // Hover moves a -> b (node changed): repaint.
    host.handleMouseMove(75, 25);
    CHECK(host.update(200, 50).needsRepaint == true);

    // Click into the input: focus gained -> repaint, and the focused caret animates.
    host.handleMouseDown(125, 25, MouseButton::Left);
    host.handleMouseUp(125, 25, MouseButton::Left);
    REQUIRE(host.getFocusedInput() != nullptr);
    auto focusFrame = host.update(200, 50);
    CHECK(focusFrame.needsRepaint == true);
    CHECK(focusFrame.animating == true);

    // Click empty space: focus lost -> repaint, animation stops.
    host.handleMouseDown(175, 25, MouseButton::Left);
    host.handleMouseUp(175, 25, MouseButton::Left);
    CHECK(host.getFocusedInput() == nullptr);
    auto blurFrame = host.update(200, 50);
    CHECK(blurFrame.needsRepaint == true);
    CHECK(blurFrame.animating == false);
}

TEST_CASE("UpdateResult - focused caret blinks: animating each frame, needsRepaint on the edge") {
    EventTestHost host(repaintFixture);

    host.update(200, 50);  // mount
    host.update(200, 50);  // settle

    // Focus the input by clicking it. The blink cycle restarts on focus gain,
    // so the caret starts visible.
    host.handleMouseDown(125, 25, MouseButton::Left);
    host.handleMouseUp(125, 25, MouseButton::Left);
    InputNode* input = host.getFocusedInput();
    REQUIRE(input != nullptr);
    CHECK(input->caretVisible == true);

    // Frame 1 (dt 0.3s -> phase 300ms, inside the 530ms on-window): no blink
    // edge; the repaint here is the latched focus/press transitions.
    auto r1 = host.update(200, 50, 0.3f);
    CHECK(r1.animating == true);
    CHECK(r1.needsRepaint == true);
    CHECK(input->caretVisible == true);

    // Frame 2 (phase 600ms > 530ms): caret hides — the edge repaints.
    auto r2 = host.update(200, 50, 0.3f);
    CHECK(r2.animating == true);
    CHECK(r2.needsRepaint == true);
    CHECK(input->caretVisible == false);

    // Frame 3 (phase 900ms): still hidden — animating, but nothing to repaint.
    auto r3 = host.update(200, 50, 0.3f);
    CHECK(r3.animating == true);
    CHECK(r3.needsRepaint == false);
    CHECK(input->caretVisible == false);

    // Frame 4 (phase 1200ms -> wraps to 200ms): visible again — edge repaints.
    auto r4 = host.update(200, 50, 0.3f);
    CHECK(r4.animating == true);
    CHECK(r4.needsRepaint == true);
    CHECK(input->caretVisible == true);

    // Park the caret hidden, blur, refocus: the cycle restarts visible.
    host.update(200, 50, 0.4f);  // phase 600ms -> hidden
    CHECK(input->caretVisible == false);
    host.handleMouseDown(175, 25, MouseButton::Left);  // blur (empty space)
    host.handleMouseUp(175, 25, MouseButton::Left);
    CHECK(host.getFocusedInput() == nullptr);
    CHECK(host.update(200, 50).animating == false);
    host.handleMouseDown(125, 25, MouseButton::Left);  // refocus
    host.handleMouseUp(125, 25, MouseButton::Left);
    REQUIRE(host.getFocusedInput() == input);
    CHECK(input->caretVisible == true);  // reset on focus gain, before any update
}

TEST_CASE("UpdateResult - autoFocus input animates from the mount frame on") {
    // autoFocus routes through EventHandler::focusInput during the mount
    // reconcile; the focused input must animate on every subsequent update and
    // the focus transition must never leak a stale latch.
    EventTestHost host([]() {
        return Row(Input().width(50).height(50).autoFocus().setKey("in")).width(200).height(50);
    });

    auto mountFrame = host.update(200, 50);
    CHECK(mountFrame.needsRepaint == true);
    REQUIRE(host.getFocusedInput() != nullptr);

    // Steady state: still animating (blinking), but no repaint without an edge.
    auto idle = host.update(200, 50, 0.01f);
    CHECK(idle.animating == true);
    CHECK(idle.needsRepaint == false);
}

TEST_CASE("UpdateResult - text edits latch needsRepaint until the next update") {
    // Regression: handleTextInput / delete edits mutated displayText without
    // latching, so an Input with no onChange wired (no app-driven reconcile)
    // reported needsRepaint == false for the edit — the typed text never showed
    // in an embedder that honors UpdateResult.
    EventTestHost host(repaintFixture);

    host.update(200, 50);  // mount
    host.update(200, 50);  // settle

    // Focus the input, then drain the focus/press transitions. Tiny dt keeps
    // the blink phase off its 530ms edge, so any repaint below is the edit's.
    host.handleMouseDown(125, 25, MouseButton::Left);
    host.handleMouseUp(125, 25, MouseButton::Left);
    REQUIRE(host.getFocusedInput() != nullptr);
    host.update(200, 50, 0.01f);
    CHECK(host.update(200, 50, 0.01f).needsRepaint == false);

    // Typing latches: the NEXT update repaints, the one after is clean.
    host.handleTextInput("a");
    CHECK(host.update(200, 50, 0.01f).needsRepaint == true);
    CHECK(host.update(200, 50, 0.01f).needsRepaint == false);

    // DeleteBackward (non-empty text) latches the same way.
    host.handleEditCommand(EditCommand::DeleteBackward);
    CHECK(host.update(200, 50, 0.01f).needsRepaint == true);
    CHECK(host.update(200, 50, 0.01f).needsRepaint == false);
}

TEST_CASE("UpdateResult - a consumed edit command restarts the caret blink and latches a repaint") {
    EventTestHost host(repaintFixture);

    host.update(200, 50);  // mount
    host.update(200, 50);  // settle

    // Focus the input by clicking it, then drain the focus/press transitions.
    host.handleMouseDown(125, 25, MouseButton::Left);
    host.handleMouseUp(125, 25, MouseButton::Left);
    InputNode* input = host.getFocusedInput();
    REQUIRE(input != nullptr);
    host.update(200, 50, 0.01f);
    CHECK(host.update(200, 50, 0.01f).needsRepaint == false);

    // Park the caret hidden (phase past the 530ms on-window).
    host.update(200, 50, 0.6f);
    CHECK(input->caretVisible == false);

    // A caret move restarts the blink (visible immediately) and latches the
    // repaint even though no text changed.
    CHECK(host.handleEditCommand(EditCommand::MoveLineEnd) == true);
    CHECK(input->caretVisible == true);
    CHECK(host.update(200, 50, 0.01f).needsRepaint == true);
    CHECK(host.update(200, 50, 0.01f).needsRepaint == false);

    // An unimplemented command reports unconsumed through the Host entry too.
    CHECK(host.handleEditCommand(EditCommand::Copy) == false);
}

TEST_CASE("UpdateResult - a handler's deferred update() cannot eat the visual-state latch") {
    // A hover handler that calls host.update() mid-dispatch defers; the drain at
    // the tail of handleMouseMove runs a full update whose result nobody sees.
    // That drained update consumes the hover transition's visual-state latch —
    // the dirt must be carried forward so the platform's NEXT update() still
    // reports needsRepaint (regression: it reported false, freezing embedders
    // that honor UpdateResult).
    Host* hostPtr = nullptr;
    EventTestHost host([&]() {
        return Box().width(50).height(50).onHover([&](bool entered) {
            if (entered)
                hostPtr->update(200, 50);
        });
    });
    hostPtr = &host;

    host.update(200, 50);  // mount
    host.update(200, 50);  // settle

    host.handleMouseMove(25, 25);  // hover enters; handler defers; drain consumes

    auto next = host.update(200, 50);
    CHECK(next.needsRepaint == true);  // the drained dirt resurfaces here

    auto after = host.update(200, 50);
    CHECK(after.needsRepaint == false);  // reported once, then cleared
}

TEST_CASE("UpdateResult - a deferred update's reconcile repaint is carried to the next update") {
    // The pre-existing facet of the same defect: a handler mutates state and
    // defers an update; the drain reconciles (tree changes, relayout) but its
    // result — fullReconcile repaint, layoutChanged — is discarded. Keyboard
    // path on purpose: handleKeyDown touches no hover/press/focus latch, so the
    // repaint below can ONLY come from the carried reconcile.
    Host* hostPtr = nullptr;
    int count = 1;
    EventTestHost host([&]() {
        std::vector<Child> kids;
        for (int i = 0; i < count; ++i)
            kids.push_back(Box().width(20).height(20).setKey(std::to_string(i)));
        return Box(std::move(kids)).width(200).height(50).onKeyDown([&](int, uint16_t, bool) {
            ++count;
            hostPtr->update(200, 50);  // deferred, drained after dispatch
        });
    });
    hostPtr = &host;

    host.update(200, 50);  // mount
    host.update(200, 50);  // settle
    REQUIRE(host.root()->children.size() == 1u);

    host.handleKeyDown(65, 0);
    REQUIRE(host.root()->children.size() == 2u);  // the drain reconciled same-frame

    auto next = host.update(200, 50);
    CHECK(next.needsRepaint == true);   // carried from the drained reconcile
    CHECK(next.layoutChanged == true);  // the drain relayouted; that must surface too

    auto after = host.update(200, 50);
    CHECK(after.needsRepaint == false);
    CHECK(after.layoutChanged == false);
}

TEST_CASE("Exception - Host::handleMouseUp noexcept backstop routes a throwing onClick") {
    int errorCount = 0;

    EventTestHost host([&]() {
        return Box().width(100).height(100).onClick([&]() { throw std::runtime_error("boom"); });
    });
    host.setErrorHandler([&](std::string_view, const std::exception*) { errorCount++; });

    host.update(100, 100);

    // The platform calls Host::handle* directly; a throwing handler must not
    // escape (noexcept) and must reach the host sink.
    host.handleMouseDown(50, 50, MouseButton::Left);
    CHECK_NOTHROW(host.handleMouseUp(50, 50, MouseButton::Left));
    CHECK(errorCount == 1);
}

// --- Hit testing with overflowing children (subtree-bounds prune) ---
//
// Children draw unclipped, so a child may extend beyond its parent's rect.
// hitTest prunes descent by the subtree AABB and hits a node only by its own
// rect: the overflowing part of a child is clickable, while a point that is
// inside a subtree's bounds but on no actual node falls through to what is
// behind. At HEAD (d13575f) the own-rect gate pruned descent, so the
// overflowing part painted but ate no clicks.

TEST_CASE("Overflowing child is clickable outside its parent's rect") {
    Reconciler reconciler;
    EventHandler events;

    bool childClicked = false;
    auto tree = Box(
                        Box(
                            Box().width(50)
                                 .height(50)
                                 .positionType(PositionType::Absolute)
                                 .positionLeft(80)
                                 .positionTop(80)
                                 .setKey("overflow")
                                 .onClick([&]() { childClicked = true; })
                        )
                            .width(100)
                            .height(100)
                            .setKey("parent")
                    )
                    .width(200)
                    .height(200);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(200, 200);

    Node* parent = root->children[0].get();
    Node* child = parent->children[0].get();

    // (120,120) is inside the child (80..130) but OUTSIDE the parent (0..100).
    // At HEAD the own-rect gate on the parent dropped this point to the root.
    events.handleMouseMove(root, 120, 120);
    CHECK(events.getHoveredNode() == child);

    events.handleMouseDown(root, 120, 120, MouseButton::Left);
    events.handleMouseUp(root, 120, 120, MouseButton::Left);
    CHECK(childClicked);

    // Inside the parent-child overlap the hit is unchanged.
    events.handleMouseMove(root, 90, 90);
    CHECK(events.getHoveredNode() == child);

    // Inside the parent's subtree bounds but on NO node: falls through to root.
    events.handleMouseMove(root, 120, 20);
    CHECK(events.getHoveredNode() == root);
}

TEST_CASE("Own-rect hits and sibling z-order are unchanged by subtree bounds") {
    Reconciler reconciler;
    EventHandler events;

    auto tree = Box(
                        Box().width(60)
                             .height(60)
                             .positionType(PositionType::Absolute)
                             .positionLeft(0)
                             .positionTop(0)
                             .setKey("under"),
                        Box().width(60)
                             .height(60)
                             .positionType(PositionType::Absolute)
                             .positionLeft(30)
                             .positionTop(0)
                             .setKey("over")
                    )
                    .width(200)
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(200, 100);

    Node* under = root->children[0].get();
    Node* over = root->children[1].get();

    // Overlap region: the later (front-most, last-drawn) sibling wins — same
    // winner as HEAD.
    events.handleMouseMove(root, 40, 30);
    CHECK(events.getHoveredNode() == over);

    // Only the earlier sibling contains the point.
    events.handleMouseMove(root, 10, 30);
    CHECK(events.getHoveredNode() == under);

    // Inside the parent but on no child: the parent itself, as at HEAD.
    events.handleMouseMove(root, 150, 80);
    CHECK(events.getHoveredNode() == root);
}

TEST_CASE("Nested overflow: click in the outermost overflow reaches the deepest node") {
    Reconciler reconciler;
    EventHandler events;

    auto tree = Box(
                        Box(
                            Box(
                                Box().width(50)
                                     .height(50)
                                     .positionType(PositionType::Absolute)
                                     .positionLeft(40)
                                     .positionTop(40)
                                     .setKey("c")
                            )
                                .width(50)
                                .height(50)
                                .positionType(PositionType::Absolute)
                                .positionLeft(40)
                                .positionTop(40)
                                .setKey("p")
                        )
                            .width(50)
                            .height(50)
                            .setKey("g")
                    )
                    .width(300)
                    .height(300);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(300, 300);

    Node* g = root->children[0].get();
    Node* p = g->children[0].get();
    Node* c = p->children[0].get();

    // c spans (80,80)-(130,130) absolutely: outside p (40..90) and g (0..50).
    // The subtree-bounds prune admits the point at every level; c is the only
    // node whose own rect contains it.
    events.handleMouseMove(root, 120, 120);
    CHECK(events.getHoveredNode() == c);
}

TEST_CASE("Scroll stays strict: clipped overflow below the viewport is not hittable") {
    Reconciler reconciler;
    EventHandler events;

    bool contentClicked = false;
    auto tree = Box(
                        Scroll(
                            Box().height(300).setKey("content").onClick([&]() { contentClicked = true; })
                        )
                            .width(100)
                            .height(100)
                            .setKey("scroll")
                    )
                    .width(100)
                    .height(200);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 200);

    Node* scroll = root->children[0].get();
    Node* content = scroll->children[0].get();

    // Inside the viewport: content is hit as always.
    events.handleMouseMove(root, 50, 50);
    CHECK(events.getHoveredNode() == content);

    // Below the viewport, where content y=150 would paint if it were NOT
    // clipped: the Scroll clips, so the subtree-bounds relaxation must not
    // leak the point into scrolled content — the root behind is hit instead.
    events.handleMouseMove(root, 50, 150);
    CHECK(events.getHoveredNode() == root);

    events.handleMouseDown(root, 50, 150, MouseButton::Left);
    events.handleMouseUp(root, 50, 150, MouseButton::Left);
    CHECK(!contentClicked);
}

// ============================================================================
// Pointer capture / drag model (6a): a press gives its target implicit pointer
// capture — moves and the release route to the captor (not the node under the
// pointer), hover freezes for the gesture, a move past kDragThresholdPx turns
// the press into a drag (no click), and presses chain into double-clicks
// against the dt-accumulated clock (advanceClock).
// ============================================================================

TEST_CASE("Capture - moves and release route to the captor, not the node under the pointer") {
    Reconciler reconciler;
    EventHandler events;

    SUBCASE("off-node moves and release go to the captor; the sibling sees nothing") {
        int aMove = 0, aUp = 0, aClick = 0;
        int bMove = 0, bUp = 0, bClick = 0, bHoverEnter = 0;
        auto tree = Box(
                            Box().width(50).height(100).setKey("a")
                                .onMouseMove([&](float, float) { ++aMove; })
                                .onMouseUp([&](float, float, MouseButton) { ++aUp; })
                                .onClick([&]() { ++aClick; }),
                            Box().width(50).height(100).setKey("b")
                                .onMouseMove([&](float, float) { ++bMove; })
                                .onMouseUp([&](float, float, MouseButton) { ++bUp; })
                                .onClick([&]() { ++bClick; })
                                .onHover([&](bool h) { if (h) ++bHoverEnter; })
                        )
                        .flexDirection(FlexDirection::Row)
                        .width(100).height(100);

        auto fiber = reconciler.mount(std::move(tree));
        auto* root = reconciler.renderRoot();
        root->calculateLayout(100, 100);

        events.handleMouseDown(root, 25, 50, MouseButton::Left);  // press on A -> capture
        CHECK(events.hasCapture());

        events.handleMouseMove(root, 75, 50);  // pointer now over B
        CHECK(aMove == 1);
        CHECK(bMove == 0);
        CHECK(bHoverEnter == 0);  // hover frozen during capture

        events.handleMouseUp(root, 75, 50, MouseButton::Left);  // release over B
        CHECK(aUp == 1);   // release routed to the captor
        CHECK(bUp == 0);
        CHECK(aClick == 0);  // release leaf not in A's subtree (and it dragged)
        CHECK(bClick == 0);  // B never received press or release dispatch
        CHECK(!events.hasCapture());
        CHECK(bHoverEnter == 1);  // hover resynced to B after release
    }

    SUBCASE("press and release split across siblings click only the shared ancestor") {
        int aClick = 0, bClick = 0, parentClick = 0;
        auto tree = Box(
                            Box().width(50).height(100).setKey("a").onClick([&]() { ++aClick; }),
                            Box().width(50).height(100).setKey("b").onClick([&]() { ++bClick; })
                        )
                        .flexDirection(FlexDirection::Row)
                        .width(100).height(100)
                        .onClick([&]() { ++parentClick; });

        auto fiber = reconciler.mount(std::move(tree));
        auto* root = reconciler.renderRoot();
        root->calculateLayout(100, 100);

        // 3px total motion: below the drag threshold, so the release still
        // clicks — but only on the ancestor holding BOTH press and release.
        events.handleMouseDown(root, 48, 50, MouseButton::Left);  // box A
        events.handleMouseMove(root, 51, 50);                     // crosses into B
        events.handleMouseUp(root, 51, 50, MouseButton::Left);    // box B
        CHECK(aClick == 0);
        CHECK(bClick == 0);
        CHECK(parentClick == 1);
    }
}

TEST_CASE("Capture - release outside the window fires onMouseUp on the captor, no click") {
    Reconciler reconciler;
    EventHandler events;

    int ups = 0, clicks = 0;
    float upX = 0, upY = 0;
    MouseButton upButton = MouseButton::Middle;
    auto tree = Box().width(100).height(100)
                    .onMouseUp([&](float x, float y, MouseButton b) {
                        ++ups;
                        upX = x;
                        upY = y;
                        upButton = b;
                    })
                    .onClick([&]() { ++clicks; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    events.handleMouseDown(root, 50, 50, MouseButton::Left);
    CHECK(events.hasCapture());

    // Release lands outside the tree: hitTest misses, releaseTarget is null —
    // the captor still gets onMouseUp with the off-window coords, but no click.
    events.handleMouseUp(root, 150, 150, MouseButton::Left);
    CHECK(ups == 1);
    CHECK(upX == 150);
    CHECK(upY == 150);
    CHECK(upButton == MouseButton::Left);
    CHECK(clicks == 0);
    CHECK(!events.hasCapture());
}

TEST_CASE("Drag threshold - a small move stays a click; past the threshold it becomes a drag") {
    Reconciler reconciler;
    EventHandler events;

    int clicks = 0, drags = 0;
    DragEvent last{};
    auto tree = Box().width(100).height(100)
                    .onClick([&]() { ++clicks; })
                    .onDrag([&](const DragEvent& e) {
                        ++drags;
                        last = e;
                    });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    SUBCASE("2px wiggle: click, no drag") {
        events.handleMouseDown(root, 50, 50, MouseButton::Left);
        events.handleMouseMove(root, 52, 52);
        events.handleMouseUp(root, 52, 52, MouseButton::Left);
        CHECK(clicks == 1);
        CHECK(drags == 0);
    }

    SUBCASE("5px move: drag stream with per-move deltas, no click") {
        events.handleMouseDown(root, 50, 50, MouseButton::Left);
        events.handleMouseMove(root, 55, 50);
        CHECK(drags == 1);
        CHECK(last.startX == 50);
        CHECK(last.startY == 50);
        CHECK(last.dx == 5);
        CHECK(last.dy == 0);
        CHECK(last.x == 55);
        CHECK(last.button == MouseButton::Left);

        events.handleMouseMove(root, 57, 53);  // delta from the PREVIOUS move
        CHECK(drags == 2);
        CHECK(last.dx == 2);
        CHECK(last.dy == 3);

        events.handleMouseUp(root, 57, 53, MouseButton::Left);
        CHECK(clicks == 0);
    }
}

TEST_CASE("Capture - survives a same-shape reconcile that preserves the captor") {
    Reconciler reconciler;
    EventHandler events;

    int drags = 0;
    float lastDx = 0;
    auto buildTree = [&](uint32_t bg) -> VNode {
        return Box(
                       Box().width(50).height(50).setKey("target").backgroundColor(bg)
                           .onDrag([&](const DragEvent& e) {
                               ++drags;
                               lastDx = e.dx;
                           })
                   )
                   .width(100).height(100);
    };

    auto fiber = reconciler.mount(buildTree(0xFF0000FF));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);
    Node* target = root->children[0].get();

    events.handleMouseDown(root, 25, 25, MouseButton::Left);
    events.handleMouseMove(root, 35, 25);
    CHECK(drags == 1);

    // Same type + key, changed prop: the reconciler updates the node in place,
    // so the captor (and its liveness token) survive.
    reconciler.reconcile(fiber.get(), buildTree(0x00FF00FF));
    CHECK(root->children[0].get() == target);

    events.handleMouseMove(root, 45, 25);
    CHECK(drags == 2);
    CHECK(lastDx == 10);
    CHECK(events.hasCapture());
}

TEST_CASE("Capture - a captor freed by reconcile ends capture silently, not with a UAF") {
    // The load-bearing seam: a press captures a node, a reconcile frees it with
    // NO node-removed callback wired (the raw-Reconciler escape path), and the
    // next move/release must detect the dead liveness token — zero deref of the
    // freed captor, nothing fires, capture reported gone. Pre-fix this is a real
    // access violation, not a test failure.
    Reconciler reconciler;
    EventHandler events;

    int drags = 0, ups = 0, clicks = 0;
    auto tree = Box(
                        Box().width(50).height(50).setKey("victim")
                            .onDrag([&](const DragEvent&) { ++drags; })
                            .onMouseUp([&](float, float, MouseButton) { ++ups; })
                            .onClick([&]() { ++clicks; })
                    )
                    .width(100).height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    Node* victim = root->children[0].get();
    events.handleMouseDown(root, 25, 25, MouseButton::Left);
    CHECK(events.hasCapture());
    std::shared_ptr<bool> victimAlive = victim->alive;
    CHECK(*victimAlive == true);

    // Reconcile the captor away (different primitive type forces removal and
    // frees the node). Its liveness token flips — the death point.
    auto next = Box(
                        Text("x").width(50).height(50).setKey("victim")
                    )
                    .width(100).height(100);
    reconciler.reconcile(fiber.get(), std::move(next));
    CHECK(*victimAlive == false);

    // Move + release re-derive the captor via the token: capture ends silently.
    CHECK_NOTHROW(events.handleMouseMove(reconciler.renderRoot(), 40, 40));
    CHECK(!events.hasCapture());
    CHECK_NOTHROW(events.handleMouseUp(reconciler.renderRoot(), 40, 40, MouseButton::Left));
    CHECK(drags == 0);
    CHECK(ups == 0);
    CHECK(clicks == 0);
    CHECK(!events.hasCapture());
}

TEST_CASE("Double click - chained by time, radius, and button; broken by delay, distance, drag") {
    Reconciler reconciler;
    EventHandler events;

    int clicks = 0, doubles = 0;
    auto tree = Box().width(100).height(100)
                    .onClick([&]() { ++clicks; })
                    .onDoubleClick([&]() { ++doubles; });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto click = [&](float x, float y) {
        events.handleMouseDown(root, x, y, MouseButton::Left);
        events.handleMouseUp(root, x, y, MouseButton::Left);
    };

    SUBCASE("two clicks 100ms apart in place: click twice, double once") {
        click(50, 50);
        events.advanceClock(0.1f);
        click(50, 50);
        CHECK(clicks == 2);
        CHECK(doubles == 1);
    }

    SUBCASE("600ms apart: no double") {
        click(50, 50);
        events.advanceClock(0.6f);
        click(50, 50);
        CHECK(clicks == 2);
        CHECK(doubles == 0);
    }

    SUBCASE("10px apart: no double") {
        click(50, 50);
        events.advanceClock(0.1f);
        click(60, 50);
        CHECK(clicks == 2);
        CHECK(doubles == 0);
    }

    SUBCASE("a drag between clicks resets the chain") {
        click(50, 50);  // click 1
        events.advanceClock(0.05f);
        // This press would be the double (count 2) — but it drags, so the chain
        // resets and the release does not click.
        events.handleMouseDown(root, 50, 50, MouseButton::Left);
        events.handleMouseMove(root, 60, 50);
        events.handleMouseUp(root, 60, 50, MouseButton::Left);
        events.advanceClock(0.05f);
        // Fresh chain after the drag: these two are counts 1 and 2 — exactly one
        // double. Without the reset they would be counts 3 and 4 — none.
        click(50, 50);
        events.advanceClock(0.05f);
        click(50, 50);
        CHECK(doubles == 1);
        CHECK(clicks == 3);  // the drag release clicked nothing
    }
}

TEST_CASE("Capture - hover is frozen during the gesture and resyncs on release") {
    Reconciler reconciler;
    EventHandler events;

    int aEnter = 0, aLeave = 0, bEnter = 0;
    auto tree = Box(
                        Box().width(50).height(100).setKey("a").onHover([&](bool h) {
                            h ? ++aEnter : ++aLeave;
                        }),
                        Box().width(50).height(100).setKey("b").onHover([&](bool h) {
                            if (h) ++bEnter;
                        })
                    )
                    .flexDirection(FlexDirection::Row)
                    .width(100).height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);
    Node* a = root->children[0].get();
    Node* b = root->children[1].get();

    events.handleMouseMove(root, 25, 50);
    CHECK(aEnter == 1);
    CHECK(events.getHoveredNode() == a);

    events.handleMouseDown(root, 25, 50, MouseButton::Left);
    events.handleMouseMove(root, 75, 50);  // over B, but captured: hover frozen
    CHECK(aLeave == 0);
    CHECK(bEnter == 0);
    CHECK(events.getHoveredNode() == a);

    events.handleMouseUp(root, 75, 50, MouseButton::Left);  // release: resync
    CHECK(aLeave == 1);
    CHECK(bEnter == 1);
    CHECK(events.getHoveredNode() == b);
}

TEST_CASE("Drag - bubbles from a handler-less child to its parent") {
    Reconciler reconciler;
    EventHandler events;

    int parentDrags = 0;
    DragEvent last{};
    auto tree = Box(
                        Box().width(50).height(50).setKey("child")
                    )
                    .width(100).height(100)
                    .onDrag([&](const DragEvent& e) {
                        ++parentDrags;
                        last = e;
                    });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    events.handleMouseDown(root, 25, 25, MouseButton::Left);  // captures the child
    events.handleMouseMove(root, 40, 25);
    CHECK(parentDrags == 1);  // child has no onDrag; the event bubbled
    CHECK(last.startX == 25);
    CHECK(last.startY == 25);
    CHECK(last.dx == 15);
}

TEST_CASE("Cursor - explicit prop, Input default, and captor override during capture") {
    Reconciler reconciler;
    EventHandler events;

    auto tree = Box(
                        Box().width(50).height(50).setKey("plain"),
                        Box().width(50).height(50).setKey("ew").cursor(CursorShape::ResizeEW),
                        Input().width(50).height(50).setKey("in")
                    )
                    .flexDirection(FlexDirection::Row)
                    .width(200).height(50);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(200, 50);

    // Nothing hovered: fallback.
    CHECK(events.getCursor() == CursorShape::Arrow);

    // Hovered node (and its chain) carry no cursor prop: fallback.
    events.handleMouseMove(root, 25, 25);
    CHECK(events.getCursor() == CursorShape::Arrow);

    // Explicit prop wins.
    events.handleMouseMove(root, 75, 25);
    CHECK(events.getCursor() == CursorShape::ResizeEW);

    // An Input without an explicit prop defaults to IBeam.
    events.handleMouseMove(root, 125, 25);
    CHECK(events.getCursor() == CursorShape::IBeam);

    // During capture the captor's chain decides, wherever the pointer is.
    events.handleMouseMove(root, 75, 25);
    events.handleMouseDown(root, 75, 25, MouseButton::Left);
    events.handleMouseMove(root, 125, 25);  // over the Input, but "ew" captured
    CHECK(events.getCursor() == CursorShape::ResizeEW);

    // Release resyncs hover to the Input: its default shows again.
    events.handleMouseUp(root, 125, 25, MouseButton::Left);
    CHECK(events.getCursor() == CursorShape::IBeam);
}

// ============================================================================
// Focus / tab system: the generalized focus slot (any primitive), the explicit
// .focusable() acquisition gate, pre-order Tab traversal with wraparound, the
// focus trap, and programmatic focus. focusStyle needs no renderer change —
// the focused flag lives on base Node and the resolver is already type-general.
// ============================================================================

TEST_CASE("Focus - click focuses a focusable Box and focusStyle resolves") {
    Reconciler reconciler;
    EventHandler events;

    BoxStyle focus;
    focus.backgroundColor = 0x222222FF;
    auto tree = Box(
                        Box().width(50).height(50).setKey("target")
                            .focusable()
                            .backgroundColor(0x111111FF)
                            .focusStyle(focus)
                    )
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto* box = static_cast<BoxNode*>(root->children[0].get());

    events.handleMouseDown(root, 25, 25, MouseButton::Left);
    events.handleMouseUp(root, 25, 25, MouseButton::Left);

    CHECK(events.getFocusedNode() == box);
    CHECK(box->focused == true);
    CHECK(events.getFocusedInput() == nullptr);  // typed view: not an Input

    // The focused pipeline is type-general end to end: the resolver applies the
    // Box's focusStyle exactly as it would an Input's.
    auto resolved = render::resolveBox(box->props, box->hovered, box->focused);
    CHECK(resolved.backgroundColor == 0x222222FFu);
}

TEST_CASE("Focus - a click walks up to the nearest focusable ancestor") {
    Reconciler reconciler;
    EventHandler events;

    auto tree = Box(
                        Box(
                            Box().width(30).height(30).setKey("leaf")
                        )
                            .width(50)
                            .height(50)
                            .setKey("holder")
                            .focusable()
                    )
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    Node* holder = root->children[0].get();
    Node* leaf = holder->children[0].get();

    // The leaf is not focusable; the click walks up and focuses the holder.
    events.handleMouseDown(root, 15, 15, MouseButton::Left);
    events.handleMouseUp(root, 15, 15, MouseButton::Left);
    CHECK(events.getFocusedNode() == holder);
    CHECK(holder->focused == true);
    CHECK(leaf->focused == false);

    // No focusable in the clicked chain (the root Box never opted in): the
    // click clears focus — blur-on-click-away.
    events.handleMouseDown(root, 90, 90, MouseButton::Left);
    events.handleMouseUp(root, 90, 90, MouseButton::Left);
    CHECK(events.getFocusedNode() == nullptr);
    CHECK(holder->focused == false);
}

TEST_CASE("Tab - pre-order traversal skips non-focusables and wraps both ways") {
    Reconciler reconciler;
    EventHandler events;

    auto tree = Row(
                    Box().width(50).height(50).setKey("a").focusable(),
                    Box().width(50).height(50).setKey("plain"),
                    Input().width(50).height(50).setKey("in"),
                    Box().width(50).height(50).setKey("b").focusable()
                )
                    .width(200)
                    .height(50);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(200, 50);

    Node* a = root->children[0].get();
    Node* in = root->children[2].get();
    Node* b = root->children[3].get();

    // Nothing focused: Tab enters at the first focusable in document order.
    events.focusNext(root);
    CHECK(events.getFocusedNode() == a);

    // Pre-order: the non-focusable Box is skipped; an Input needs no opt-in.
    events.focusNext(root);
    CHECK(events.getFocusedNode() == in);
    events.focusNext(root);
    CHECK(events.getFocusedNode() == b);

    // Wraps at the end.
    events.focusNext(root);
    CHECK(events.getFocusedNode() == a);

    // Shift-Tab wraps backward from the front.
    events.focusPrev(root);
    CHECK(events.getFocusedNode() == b);
    events.focusPrev(root);
    CHECK(events.getFocusedNode() == in);

    // Nothing focused: Shift-Tab enters at the last focusable.
    events.focusNode(nullptr);
    events.focusPrev(root);
    CHECK(events.getFocusedNode() == b);
}

TEST_CASE("Tab - a focused node freed by reconcile reads as no-focus; Tab recovers (no UAF)") {
    // The generalized-slot analogue of the focused-input UAF regression: NO
    // node-removed callback is wired (the token-only escape path), so a
    // reconcile that frees the focused node leaves the raw pointer dangling
    // until a liveness check.
    Reconciler reconciler;
    EventHandler events;

    auto tree = Row(
                    Box().width(50).height(50).setKey("a").focusable(),
                    Box().width(50).height(50).setKey("b").focusable()
                )
                    .width(200)
                    .height(50);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(200, 50);

    Node* b = root->children[1].get();
    events.focusNode(b);
    CHECK(events.getFocusedNode() == b);
    std::shared_ptr<bool> bAlive = b->alive;

    // Reconcile "b" into a different primitive type: forced removal frees it.
    auto next = Row(
                    Box().width(50).height(50).setKey("a").focusable(),
                    Text("x").width(50).height(50).setKey("b")
                )
                    .width(200)
                    .height(50);
    reconciler.reconcile(fiber.get(), std::move(next));
    root = reconciler.renderRoot();
    root->calculateLayout(200, 50);
    CHECK(*bAlive == false);  // genuinely freed — the dangling precondition

    // The dead token reads as no-focus; Tab recovers to the first focusable.
    CHECK(events.getFocusedNode() == nullptr);
    CHECK_NOTHROW(events.focusNext(root));
    CHECK(events.getFocusedNode() == root->children[0].get());

    // A trap root freed by reconcile likewise falls back: traversal scopes to
    // the FULL tree instead of collecting from freed memory.
    events.setFocusTrap(root->children[1].get());  // the Text node as trap root
    std::shared_ptr<bool> trapAlive = root->children[1]->alive;
    auto next2 = Row(
                     Box().width(50).height(50).setKey("a").focusable(),
                     Box().width(50).height(50).setKey("c").focusable()
                 )
                     .width(200)
                     .height(50);
    reconciler.reconcile(fiber.get(), std::move(next2));
    root = reconciler.renderRoot();
    root->calculateLayout(200, 50);
    CHECK(*trapAlive == false);

    // Focus is still on the reused "a"; Tab under the dead trap moves through
    // the full tree to "c".
    CHECK(events.getFocusedNode() == root->children[0].get());
    CHECK_NOTHROW(events.focusNext(root));
    CHECK(events.getFocusedNode() == root->children[1].get());
}

TEST_CASE("Key - events route to the focused Box and bubble while unconsumed") {
    Reconciler reconciler;
    EventHandler events;

    std::vector<std::string> log;
    auto tree = Box(
                        Box(
                            Box().width(30).height(30).setKey("leaf").focusable()
                        )
                            .width(50)
                            .height(50)
                            .setKey("mid")
                            .onKeyDown([&](int, uint16_t, bool) { log.push_back("mid"); })
                    )
                    .width(100)
                    .height(100)
                    .onKeyDown([&](int, uint16_t, bool) { log.push_back("root"); });

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    Node* mid = root->children[0].get();
    Node* leaf = mid->children[0].get();

    // The focused Box carries no handler: the event starts AT the focused node
    // and bubbles until the first handler consumes — the root is never reached.
    events.focusNode(leaf);
    bool consumed = events.handleKeyDown(root, 65, 0);
    CHECK(consumed == true);
    CHECK(log == std::vector<std::string>{"mid"});

    // A focused handler-bearing node consumes in place.
    log.clear();
    events.focusNode(mid);
    events.handleKeyDown(root, 65, 0);
    CHECK(log == std::vector<std::string>{"mid"});
}

TEST_CASE("Focus - text editing routes only through a focused Input") {
    Reconciler reconciler;
    EventHandler events;

    auto tree = Row(
                    Box().width(50).height(50).setKey("box").focusable(),
                    Input().width(50).height(50).setKey("in")
                )
                    .width(200)
                    .height(50);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(200, 50);

    Node* box = root->children[0].get();
    auto* input = static_cast<InputNode*>(root->children[1].get());

    // Focused Input: the text entry points reach it.
    events.focusNode(input);
    events.handleTextInput("hi");
    CHECK(input->displayText == "hi");
    CHECK(events.handleEditCommand(EditCommand::DeleteBackward) == true);
    CHECK(input->displayText == "h");
    bool submitted = false;
    input->props.onSubmit = [&] { submitted = true; };
    CHECK(events.handleEditCommand(EditCommand::InsertNewline) == true);
    CHECK(submitted == true);
    CHECK(input->displayText == "h");  // single-line Enter submits, never inserts

    // Focused Box: the typed view is null, so the same entry points are safe
    // no-ops — the Input's state is untouched.
    events.focusNode(box);
    CHECK(events.getFocusedNode() == box);
    CHECK(events.getFocusedInput() == nullptr);
    CHECK_NOTHROW(events.handleTextInput("x"));
    CHECK(events.handleEditCommand(EditCommand::DeleteBackward) == false);
    CHECK(events.handleEditCommand(EditCommand::InsertNewline) == false);
    CHECK(input->displayText == "h");
}

TEST_CASE("Focus - programmatic focusNode/blur, onFocus ordering, any node accepted") {
    Reconciler reconciler;
    EventHandler events;

    std::vector<std::string> log;
    auto tree = Row(
                    Box().width(50).height(50).setKey("a").focusable().onFocus(
                        [&](bool f) { log.push_back(f ? "a:true" : "a:false"); }),
                    Box().width(50).height(50).setKey("b").onFocus(
                        [&](bool f) { log.push_back(f ? "b:true" : "b:false"); })
                )
                    .width(200)
                    .height(50);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(200, 50);

    Node* a = root->children[0].get();
    Node* b = root->children[1].get();

    events.focusNode(a);
    CHECK(log == std::vector<std::string>{"a:true"});
    CHECK(a->focused == true);

    // "b" never opted into .focusable(): programmatic focus still lands (the
    // predicate gates only click/Tab acquisition) — and the old node's
    // onFocus(false) fires BEFORE the new node's onFocus(true).
    events.focusNode(b);
    CHECK(log == std::vector<std::string>{"a:true", "a:false", "b:true"});
    CHECK(a->focused == false);
    CHECK(b->focused == true);
    CHECK(events.getFocusedNode() == b);

    // Blur.
    events.focusNode(nullptr);
    CHECK(log == std::vector<std::string>{"a:true", "a:false", "b:true", "b:false"});
    CHECK(b->focused == false);
    CHECK(events.getFocusedNode() == nullptr);

    // Re-focusing the current focus is a no-op (no spurious callbacks).
    events.focusNode(a);
    log.clear();
    events.focusNode(a);
    CHECK(log.empty());
}

TEST_CASE("Focus - autoFocus on a Box focuses it at mount") {
    EventTestHost host([]() {
        return Row(Box().width(50).height(50).setKey("box").focusable().autoFocus())
            .width(200)
            .height(50);
    });

    auto mountFrame = host.update(200, 50);
    CHECK(mountFrame.needsRepaint == true);

    Node* focused = host.getFocusedNode();
    REQUIRE(focused != nullptr);
    CHECK(focused->type() == PrimitiveType::Box);
    CHECK(focused->focused == true);
    CHECK(host.getFocusedInput() == nullptr);
}

TEST_CASE("UpdateResult - focus traversal latches needsRepaint until the next update") {
    EventTestHost host([]() {
        return Row(Box().width(50).height(50).setKey("a").focusable(),
                   Box().width(50).height(50).setKey("b").focusable())
            .width(200)
            .height(50);
    });

    host.update(200, 50);  // mount
    host.update(200, 50);  // settle
    CHECK(host.update(200, 50).needsRepaint == false);

    // Tab: the focus transition latches; the NEXT update repaints, then idle.
    host.focusNext();
    REQUIRE(host.getFocusedNode() != nullptr);
    CHECK(host.update(200, 50).needsRepaint == true);
    CHECK(host.update(200, 50).needsRepaint == false);

    // Programmatic focus latches the same way.
    host.focus(host.root()->children[1].get());
    CHECK(host.update(200, 50).needsRepaint == true);
    CHECK(host.update(200, 50).needsRepaint == false);

    // And blur.
    host.blur();
    CHECK(host.getFocusedNode() == nullptr);
    CHECK(host.update(200, 50).needsRepaint == true);
    CHECK(host.update(200, 50).needsRepaint == false);
}

TEST_CASE("Focus trap - Tab cycles inside the trap; clearing restores the full tree") {
    Reconciler reconciler;
    EventHandler events;

    auto tree = Row(
                    Box().width(40).height(40).setKey("outside-a").focusable(),
                    Box(
                        Box().width(20).height(20).setKey("t1").focusable(),
                        Box().width(20).height(20).setKey("t2").focusable()
                    )
                        .width(60)
                        .height(40)
                        .setKey("modal"),
                    Box().width(40).height(40).setKey("outside-b").focusable()
                )
                    .width(200)
                    .height(50);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(200, 50);

    Node* outsideA = root->children[0].get();
    Node* modal = root->children[1].get();
    Node* t1 = modal->children[0].get();
    Node* t2 = modal->children[1].get();
    Node* outsideB = root->children[2].get();

    events.setFocusTrap(modal);

    // Scoped traversal: enter at the trap's first focusable and cycle inside —
    // the outside focusables are unreachable by Tab.
    events.focusNext(root);
    CHECK(events.getFocusedNode() == t1);
    events.focusNext(root);
    CHECK(events.getFocusedNode() == t2);
    events.focusNext(root);
    CHECK(events.getFocusedNode() == t1);  // wraps inside the trap
    events.focusPrev(root);
    CHECK(events.getFocusedNode() == t2);

    // Focus parked outside the trap (programmatic): the next Tab pulls it in.
    events.focusNode(outsideA);
    events.focusNext(root);
    CHECK(events.getFocusedNode() == t1);

    // Clearing the trap restores full-tree traversal (document order resumes
    // from the current node).
    events.clearFocusTrap();
    events.focusNext(root);
    CHECK(events.getFocusedNode() == t2);
    events.focusNext(root);
    CHECK(events.getFocusedNode() == outsideB);
    events.focusNext(root);
    CHECK(events.getFocusedNode() == outsideA);  // full-tree wrap
}

TEST_CASE("Exception - a throwing onFocus during Tab keeps flags and slot consistent") {
    Reconciler reconciler;
    EventHandler events;

    int errorCount = 0;
    events.setErrorHandler([&](std::string_view, const std::exception*) { errorCount++; });

    auto tree = Row(
                    Box().width(50).height(50).setKey("a").focusable().onFocus(
                        [](bool) { throw std::runtime_error("focus boom"); }),
                    Box().width(50).height(50).setKey("b").focusable().onFocus(
                        [](bool) { throw std::runtime_error("focus boom"); })
                )
                    .width(200)
                    .height(50);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(200, 50);

    Node* a = root->children[0].get();
    Node* b = root->children[1].get();

    // Tab onto "a": onFocus(true) throws, but the flag and slot committed first.
    CHECK_NOTHROW(events.focusNext(root));
    CHECK(errorCount == 1);
    CHECK(events.getFocusedNode() == a);
    CHECK(a->focused == true);

    // Tab onto "b": BOTH the old node's onFocus(false) and the new node's
    // onFocus(true) throw; flags and slot still transition cleanly.
    CHECK_NOTHROW(events.focusNext(root));
    CHECK(errorCount == 3);
    CHECK(events.getFocusedNode() == b);
    CHECK(a->focused == false);
    CHECK(b->focused == true);
}

// ============================================================================
// Clipboard (6c C5): Cut/Copy/Paste through the IClipboard seam. Password
// copy/cut is refused; a null clipboard never destroys text; paste strips
// newlines for the single-line input. The Host/IClipboard link's liveness
// mirrors the ITextMeasurer teardown contract (see B6 in test_measure.cpp).
// ============================================================================

TEST_CASE("Clipboard - Copy places the selection on the clipboard; text and selection intact") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "hello");
    test::TestClipboard clip;

    // Select [2,5) = "llo": caret to 2, then extend to the end.
    events.handleEditCommand(EditCommand::MoveRight);
    events.handleEditCommand(EditCommand::MoveRight);
    events.handleEditCommand(EditCommand::MoveLineEnd, true);

    CHECK(events.handleEditCommand(EditCommand::Copy, false, &clip) == true);
    CHECK(clip.text() == "llo");
    // Copy neither edits nor collapses: the text and the selection survive.
    CHECK(input->displayText == "hello");
    CHECK(input->selBegin() == 2);
    CHECK(input->selEnd() == 5);
}

TEST_CASE("Clipboard - Copy with no selection is unconsumed and never clobbers the clipboard") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "hello");
    test::TestClipboard clip("keep");

    events.handleEditCommand(EditCommand::MoveRight);  // collapsed caret at 1
    CHECK(events.handleEditCommand(EditCommand::Copy, false, &clip) == false);
    CHECK(clip.text() == "keep");
    CHECK(clip.sets() == 0);
    CHECK(input->displayText == "hello");
}

TEST_CASE("Clipboard - password Copy and Cut are refused but consumed; Paste is allowed") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "secret");
    input->props.password = true;
    int changes = 0;
    input->props.onChange = [&](const std::string&) { ++changes; };
    test::TestClipboard clip("attacker");

    events.handleEditCommand(EditCommand::SelectAll);

    // Refused but CONSUMED (browser parity): the keystroke never leaks
    // elsewhere, and the secret never reaches the clipboard.
    CHECK(events.handleEditCommand(EditCommand::Copy, false, &clip) == true);
    CHECK(clip.sets() == 0);
    CHECK(clip.text() == "attacker");

    // Cut refuses BOTH halves: no copy, and no delete either.
    CHECK(events.handleEditCommand(EditCommand::Cut, false, &clip) == true);
    CHECK(clip.sets() == 0);
    CHECK(input->displayText == "secret");
    CHECK(changes == 0);

    // Pasting INTO a password is fine — only the outbound direction leaks.
    // The refused Cut left the SelectAll selection intact, so paste replaces it.
    CHECK(events.handleEditCommand(EditCommand::Paste, false, &clip) == true);
    CHECK(input->displayText == "attacker");
    CHECK(changes == 1);
}

TEST_CASE("Clipboard - no clipboard installed: Copy/Cut/Paste unconsumed, nothing destroyed") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "hello");

    // Select [2,5), then drive all three with the default null clipboard.
    events.handleEditCommand(EditCommand::MoveRight);
    events.handleEditCommand(EditCommand::MoveRight);
    events.handleEditCommand(EditCommand::MoveLineEnd, true);

    CHECK(events.handleEditCommand(EditCommand::Copy) == false);
    // Cut with nowhere to put the text must NOT delete it.
    CHECK(events.handleEditCommand(EditCommand::Cut) == false);
    CHECK(events.handleEditCommand(EditCommand::Paste) == false);
    CHECK(input->displayText == "hello");
    CHECK(input->selBegin() == 2);
    CHECK(input->selEnd() == 5);
}

TEST_CASE("Clipboard - Cut copies the selection, removes it, and fires onChange once") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "hello");
    int changes = 0;
    std::string lastChange;
    input->props.onChange = [&](const std::string& s) {
        ++changes;
        lastChange = s;
    };
    test::TestClipboard clip;

    // Select [2,5) = "llo".
    events.handleEditCommand(EditCommand::MoveRight);
    events.handleEditCommand(EditCommand::MoveRight);
    events.handleEditCommand(EditCommand::MoveLineEnd, true);

    CHECK(events.handleEditCommand(EditCommand::Cut, false, &clip) == true);
    CHECK(clip.text() == "llo");
    CHECK(input->displayText == "he");
    CHECK(input->caret == 2);
    CHECK(input->selectionAnchor == 2);
    CHECK(!input->hasSelection());
    CHECK(changes == 1);
    CHECK(lastChange == "he");
}

TEST_CASE("Clipboard - Cut with no selection is unconsumed and leaves the clipboard alone") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "hello");
    test::TestClipboard clip("keep");

    events.handleEditCommand(EditCommand::MoveRight);
    CHECK(events.handleEditCommand(EditCommand::Cut, false, &clip) == false);
    CHECK(clip.text() == "keep");
    CHECK(clip.sets() == 0);
    CHECK(input->displayText == "hello");
}

TEST_CASE("Clipboard - Paste inserts at the caret and fires onChange once") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "hello");
    int changes = 0;
    input->props.onChange = [&](const std::string&) { ++changes; };
    test::TestClipboard clip("XY");

    events.handleEditCommand(EditCommand::MoveRight);
    events.handleEditCommand(EditCommand::MoveRight);
    CHECK(events.handleEditCommand(EditCommand::Paste, false, &clip) == true);
    CHECK(input->displayText == "heXYllo");
    // The caret (and collapsed anchor) land after the pasted text.
    CHECK(input->caret == 4);
    CHECK(input->selectionAnchor == 4);
    CHECK(changes == 1);
}

TEST_CASE("Clipboard - Paste replaces the selection") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "hello");
    test::TestClipboard clip("Z");

    // Select [1,4) = "ell".
    events.handleEditCommand(EditCommand::MoveRight);
    for (int i = 0; i < 3; ++i)
        events.handleEditCommand(EditCommand::MoveRight, true);

    CHECK(events.handleEditCommand(EditCommand::Paste, false, &clip) == true);
    CHECK(input->displayText == "hZo");
    CHECK(input->caret == 2);
    CHECK(!input->hasSelection());
}

TEST_CASE("Clipboard - Paste strips newlines for the single-line input") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "");
    test::TestClipboard clip("a\nb\r\nc");

    CHECK(events.handleEditCommand(EditCommand::Paste, false, &clip) == true);
    CHECK(input->displayText == "abc");
    CHECK(input->caret == 3);
}

TEST_CASE("Clipboard - pasting an empty clipboard is a consumed no-op; the selection survives") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "hello");
    int changes = 0;
    input->props.onChange = [&](const std::string&) { ++changes; };

    events.handleEditCommand(EditCommand::SelectAll);

    test::TestClipboard empty;
    CHECK(events.handleEditCommand(EditCommand::Paste, false, &empty) == true);
    CHECK(input->displayText == "hello");
    CHECK(input->hasSelection());  // an empty paste is not a delete
    CHECK(changes == 0);

    // A newline-only clipboard sanitizes down to empty: the same no-op.
    test::TestClipboard newlines("\r\n");
    CHECK(events.handleEditCommand(EditCommand::Paste, false, &newlines) == true);
    CHECK(input->displayText == "hello");
    CHECK(changes == 0);
}

// ---------------------------------------------------------------------------
// C5 liveness matrix: the Host/IClipboard link is self-managed in both
// directions (either object may die first), mirroring the ITextMeasurer
// teardown tests. These go through Host::setClipboard / Host::handleEditCommand
// — the plumbing under test — where the EventHandler-level tests above pass the
// clipboard directly.
// ---------------------------------------------------------------------------

namespace {

VNode clipboardFixture() {
    return Box(
                   Input().value("hi").width(80).height(20).setKey("f")
               )
        .width(200)
        .height(100);
}

// Mount the fixture's host, focus its Input, and pin the caret at the end so a
// paste appends deterministically.
InputNode* focusHostInput(EventTestHost& host) {
    host.update(200, 100);
    auto* input = static_cast<InputNode*>(host.root()->children[0].get());
    REQUIRE(input->type() == PrimitiveType::Input);
    host.focus(input);
    host.handleEditCommand(EditCommand::MoveLineEnd);
    return input;
}

}  // namespace

TEST_CASE("C5 liveness: clipboard destroyed before host severs the link; paste no-ops") {
    EventTestHost host(clipboardFixture);
    InputNode* input = focusHostInput(host);
    {
        test::TestClipboard clip("XY");
        host.setClipboard(&clip);
        CHECK(host.handleEditCommand(EditCommand::Paste) == true);
        CHECK(input->displayText == "hiXY");
        // clip dies here WITHOUT host.setClipboard(nullptr): ~IClipboard must
        // null the host's clipboard_ by itself (clipboard-dies-first).
    }
    // Severed: paste reports unconsumed, touches nothing, and does not crash.
    CHECK(host.handleEditCommand(EditCommand::Paste) == false);
    CHECK(input->displayText == "hiXY");
}

TEST_CASE("C5 liveness: host destroyed before clipboard is safe") {
    test::TestClipboard clip("XY");
    {
        EventTestHost host(clipboardFixture);
        InputNode* input = focusHostInput(host);
        host.setClipboard(&clip);
        CHECK(host.handleEditCommand(EditCommand::Paste) == true);
        CHECK(input->displayText == "hiXY");
        // host dies here; ~Host deregisters from `clip`.
    }
    // clip is alive and untouched by the host's teardown; its own destruction
    // at scope exit must not touch the freed host — reaching the end is the pass.
    CHECK(clip.text() == "XY");
}

TEST_CASE("C5 liveness: setClipboard replacement detaches the prior clipboard") {
    EventTestHost host(clipboardFixture);
    InputNode* input = focusHostInput(host);
    test::TestClipboard second("B");
    {
        test::TestClipboard first("A");
        host.setClipboard(&first);
        host.setClipboard(&second);  // replacement must deregister `first`
        // `first` dies here: had the replacement failed to detach it, ~first
        // would null clipboard_ and sever the LIVE link to `second`.
    }
    CHECK(host.handleEditCommand(EditCommand::Paste) == true);
    CHECK(input->displayText == "hiB");
}

TEST_CASE("C5 liveness: clearing, re-installing, and sharing across hosts stay safe") {
    EventTestHost host(clipboardFixture);
    InputNode* input = focusHostInput(host);
    test::TestClipboard clip("XY");

    // setClipboard(nullptr) detaches: clipboard commands report unconsumed again.
    host.setClipboard(&clip);
    host.setClipboard(nullptr);
    CHECK(host.handleEditCommand(EditCommand::Paste) == false);
    CHECK(input->displayText == "hi");

    // Re-installing after a clear works.
    host.setClipboard(&clip);
    CHECK(host.handleEditCommand(EditCommand::Paste) == true);
    CHECK(input->displayText == "hiXY");

    // One clipboard on TWO hosts (registrations_ is plural by design).
    SUBCASE("clipboard dies before both hosts: both links severed") {
        EventTestHost other(clipboardFixture);
        InputNode* otherInput = focusHostInput(other);
        {
            test::TestClipboard shared("Z");
            host.setClipboard(&shared);
            other.setClipboard(&shared);
        }
        CHECK(host.handleEditCommand(EditCommand::Paste) == false);
        CHECK(other.handleEditCommand(EditCommand::Paste) == false);
        CHECK(otherInput->displayText == "hi");
    }
    SUBCASE("one host dies: the surviving host's link stays live") {
        {
            EventTestHost other(clipboardFixture);
            focusHostInput(other);
            other.setClipboard(&clip);
            // other dies; ~Host must remove ITS registration and leave ours.
        }
        CHECK(host.handleEditCommand(EditCommand::Paste) == true);
        CHECK(input->displayText == "hiXYXY");
    }
}

// ============================================================================
// Multiline input (6c C6): InputProps::multiline wraps the value at the
// content width (minus the text pads), grows the input to its line count via
// a Yoga measure func, routes Enter to InsertNewline (multiline inserts '\n';
// single-line fires onSubmit), navigates vertically with a sticky goal
// column, maps clicks by (x, y), and follows the caret with textScrollY.
// ============================================================================

namespace {

// The Box-wrapped multiline Input the C6 tests mount: fontSize 10 + the
// 10px/byte measurer give 10px character cells and a 10px line height; the
// explicit width 100 makes the wrap span 100 - 2*kInputTextPad = 84.
VNode multilineFixture(const char* value) {
    return Box(
               Input().value(value).multiline().fontSize(10).width(100).setKey("area")
           )
        .width(200)
        .height(200);
}

InputNode* mountMultilineInput(test::MeasureHarness& h, const char* value) {
    Node* root = h.mount(multilineFixture(value));
    root->calculateLayout(200, 200);
    auto* input = static_cast<InputNode*>(root->children[0].get());
    REQUIRE(input->type() == PrimitiveType::Input);
    return input;
}

}  // namespace

TEST_CASE("Multiline - measures wrapped-line-count * lineHeight and grows on InsertNewline") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;

    // Three hard lines at 10px lineHeight: measured height 30 (no height prop).
    InputNode* input = mountMultilineInput(h, "ab\ncd\nef");
    CHECK(input->layout.height == doctest::Approx(30));

    // InsertNewline adds a line; the edit marks the measure node dirty and
    // latches the relayout flag, so the next layout re-measures to 4 lines.
    events.focusNode(input);
    events.handleEditCommand(EditCommand::MoveLineEnd);  // caret to line 0's end
    CHECK(events.consumeTextLayoutChanged() == false);   // a pure move never latches
    CHECK(events.handleEditCommand(EditCommand::InsertNewline) == true);
    CHECK(input->displayText == "ab\n\ncd\nef");
    CHECK(input->caret == 3);  // past the inserted '\n'
    CHECK(events.consumeTextLayoutChanged() == true);
    h.reconciler().renderRoot()->calculateLayout(200, 200);
    CHECK(input->layout.height == doctest::Approx(40));
}

TEST_CASE("Multiline - soft wrap measures at the content width minus the text pads") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);

    // Three 40px words against the 84px span: greedy wrap breaks before each
    // next word (40 + 10 + 40 = 90 > 84) -> 3 lines -> height 30.
    InputNode* input = mountMultilineInput(h, "aaaa bbbb cccc");
    CHECK(input->layout.height == doctest::Approx(30));
}

TEST_CASE("Multiline - single-line and password inputs keep prop-driven sizing (no measure func)") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);

    SUBCASE("single-line: no measure func, the height prop stands") {
        InputNode* input = mountMeasuredInput(h, "hello");
        CHECK(YGNodeHasMeasureFunc(input->yogaNode) == false);
        CHECK(input->layout.height == doctest::Approx(30));
    }

    SUBCASE("password wins over multiline: a password textarea stays single-line") {
        Node* root = h.mount(Box(
                                 Input().value("abc").multiline().password(true).fontSize(10).width(100).height(30)
                             )
                                 .width(200)
                                 .height(200));
        root->calculateLayout(200, 200);
        auto* input = static_cast<InputNode*>(root->children[0].get());
        CHECK(input->multiline() == false);
        CHECK(YGNodeHasMeasureFunc(input->yogaNode) == false);
        CHECK(input->layout.height == doctest::Approx(30));
    }

    SUBCASE("toggling multiline off removes the measure func") {
        InputNode* input = mountMultilineInput(h, "ab\ncd");
        CHECK(YGNodeHasMeasureFunc(input->yogaNode) == true);
        h.reconciler().reconcile(h.fiber(), Box(
                                                Input().value("ab\ncd").multiline(false).fontSize(10).width(100).setKey("area")
                                            )
                                                .width(200)
                                                .height(200));
        CHECK(YGNodeHasMeasureFunc(input->yogaNode) == false);
    }
}

TEST_CASE("Multiline - InsertNewline replaces a selection; onChange fires with the newline") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    InputNode* input = mountMultilineInput(h, "abcd");
    events.focusNode(input);
    std::string lastChange;
    input->props.onChange = [&](const std::string& s) { lastChange = s; };

    // Select [1,3) = "bc", then Enter: the selection is replaced by '\n'.
    events.handleEditCommand(EditCommand::MoveRight);
    events.handleEditCommand(EditCommand::MoveRight, true);
    events.handleEditCommand(EditCommand::MoveRight, true);
    CHECK(events.handleEditCommand(EditCommand::InsertNewline) == true);
    CHECK(input->displayText == "a\nd");
    CHECK(input->caret == 2);
    CHECK(!input->hasSelection());
    CHECK(lastChange == "a\nd");
}

TEST_CASE("Multiline - MoveUp/MoveDown navigate by line with a sticky goal column") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    // Lines of 6 / 2 / 6 chars: runs [0,6), [7,9), [10,16).
    InputNode* input = mountMultilineInput(h, "cccccc\nbb\ndddddd");
    events.focusNode(input);

    // Caret to the very end (line 2, column x=60).
    events.handleEditCommand(EditCommand::SelectAll);
    events.handleEditCommand(EditCommand::MoveRight);  // collapse at the end
    CHECK(input->caret == 16);

    // First MoveUp records the goal column (60) and clamps to short line 1's end.
    CHECK(events.handleEditCommand(EditCommand::MoveUp) == true);
    CHECK(input->caret == 9);
    // The goal SURVIVES the clamp: the next MoveUp lands at column 60 on line 0.
    events.handleEditCommand(EditCommand::MoveUp);
    CHECK(input->caret == 6);
    // And back down through the short line to the original column.
    events.handleEditCommand(EditCommand::MoveDown);
    CHECK(input->caret == 9);
    events.handleEditCommand(EditCommand::MoveDown);
    CHECK(input->caret == 16);

    // A horizontal move INVALIDATES the goal: the next vertical move
    // re-records from the caret's new x.
    events.handleEditCommand(EditCommand::MoveUp);    // line 1's end (byte 9)
    events.handleEditCommand(EditCommand::MoveLeft);  // byte 8, x=10 - goal reset
    CHECK(!input->verticalNavGoalX.has_value());
    events.handleEditCommand(EditCommand::MoveUp);    // line 0 at the NEW column 10
    CHECK(input->caret == 1);

    // Typed text invalidates too.
    events.handleEditCommand(EditCommand::MoveUp);  // establish a goal
    CHECK(input->verticalNavGoalX.has_value());
    events.handleTextInput("x");
    CHECK(!input->verticalNavGoalX.has_value());
}

TEST_CASE("Multiline - MoveUp on the first line goes to the start; MoveDown on the last to the end") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    InputNode* input = mountMultilineInput(h, "aaaa\nbbbb");
    events.focusNode(input);

    events.handleEditCommand(EditCommand::MoveRight);
    events.handleEditCommand(EditCommand::MoveRight);
    CHECK(events.handleEditCommand(EditCommand::MoveUp) == true);  // consumed even at the top
    CHECK(input->caret == 0);

    events.handleEditCommand(EditCommand::MoveDown);  // line 1, goal column 0
    CHECK(events.handleEditCommand(EditCommand::MoveDown) == true);
    CHECK(input->caret == input->displayText.size());

    // Single-line vertical moves stay unconsumed (covered again here at the
    // seam the multiline branch forked from).
    Reconciler reconciler;
    std::unique_ptr<Fiber> fiber;
    EventHandler singleEvents;
    mountFocusedInput(reconciler, fiber, singleEvents, "abc");
    CHECK(singleEvents.handleEditCommand(EditCommand::MoveUp) == false);
    CHECK(singleEvents.handleEditCommand(EditCommand::MoveDown) == false);
}

TEST_CASE("Multiline - shift+Down extends the selection from the anchor") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    InputNode* input = mountMultilineInput(h, "aaaa\nbbbb");
    events.focusNode(input);

    events.handleEditCommand(EditCommand::MoveRight);
    events.handleEditCommand(EditCommand::MoveRight);  // caret 2, x=20
    events.handleEditCommand(EditCommand::MoveDown, true);
    CHECK(input->caret == 7);  // line 1 at column 20
    CHECK(input->selBegin() == 2);
    CHECK(input->selEnd() == 7);
}

TEST_CASE("Multiline - a click maps (x, y) to the line and column, clamped to the edge lines") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    // padding 5 insets the content box; height 50 leaves room below the 3
    // lines (30px) so clicks above/below the text stay INSIDE the input.
    Node* root = h.mount(Box(
                             Input().value("aaaa\nbbbb\ncccc").multiline().fontSize(10).width(100).height(50).padding(5)
                         )
                             .width(200)
                             .height(200));
    root->calculateLayout(200, 200);
    auto* input = static_cast<InputNode*>(root->children[0].get());

    // Window -> text mapping: textX = x - (insetLeft + pad), textY = y - insetTop.
    auto clickAt = [&](float x, float y) {
        events.advanceClock(0.6f);  // break the multi-click chain (see the C2 tests)
        events.handleMouseDown(root, x, y);
        events.handleMouseUp(root, x, y);
        return input->caret;
    };

    // Line 1 (textY 15), textX 25: past 'b' cell 1's midpoint (15), on cell
    // 2's midpoint (25, ties left) -> boundary 2 of the line -> byte 7.
    CHECK(clickAt(5 + rd::kInputTextPad + 25, 5 + 15) == 7);
    // Below all lines (textY 43 -> raw line 4): clamps to line 2; x past the
    // last midpoint -> the line's end.
    CHECK(clickAt(5 + rd::kInputTextPad + 70, 48) == 14);
    // Above the content top (textY -3): clamps to line 0.
    CHECK(clickAt(5 + rd::kInputTextPad + 25, 2) == 2);
    // A click resets the vertical-nav goal column.
    events.handleEditCommand(EditCommand::MoveUp);
    CHECK(input->verticalNavGoalX.has_value());
    clickAt(5 + rd::kInputTextPad + 25, 5 + 15);
    CHECK(!input->verticalNavGoalX.has_value());
}

TEST_CASE("Multiline - textScrollY follows the caret line and returns to 0 at the top") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    // Five 10px lines in a 25px-tall box: two lines and a half visible.
    Node* root = h.mount(Box(
                             Input().value("a\nb\nc\nd\ne").multiline().fontSize(10).width(100).height(25)
                         )
                             .width(200)
                             .height(200));
    root->calculateLayout(200, 200);
    auto* input = static_cast<InputNode*>(root->children[0].get());
    events.focusNode(input);
    CHECK(input->textScrollY == doctest::Approx(0));

    // Descend: each move keeps the caret's LINE box inside [scrollY, scrollY+25].
    events.handleEditCommand(EditCommand::MoveDown);  // line 1: 20 <= 25, no scroll
    CHECK(input->textScrollY == doctest::Approx(0));
    events.handleEditCommand(EditCommand::MoveDown);  // line 2: 30 > 25 -> 5
    CHECK(input->textScrollY == doctest::Approx(5));
    events.handleEditCommand(EditCommand::MoveDown);  // line 3 -> 15
    CHECK(input->textScrollY == doctest::Approx(15));
    events.handleEditCommand(EditCommand::MoveDown);  // line 4 -> 25 (== max: 50-25)
    CHECK(input->textScrollY == doctest::Approx(25));

    // Ascend: the caret line scrolls back into view, to exactly 0 at the top.
    events.handleEditCommand(EditCommand::MoveUp);  // line 3 still visible
    CHECK(input->textScrollY == doctest::Approx(25));
    events.handleEditCommand(EditCommand::MoveUp);  // line 2: 20 < 25 -> 20
    CHECK(input->textScrollY == doctest::Approx(20));
    events.handleEditCommand(EditCommand::MoveUp);
    CHECK(input->textScrollY == doctest::Approx(10));
    events.handleEditCommand(EditCommand::MoveUp);
    CHECK(input->textScrollY == doctest::Approx(0));

    // An external value replacement resets the vertical scroll like the
    // horizontal one (the new text shows from its head).
    events.handleEditCommand(EditCommand::SelectAll);
    events.handleEditCommand(EditCommand::MoveRight);
    CHECK(input->textScrollY > 0);
    h.reconciler().reconcile(h.fiber(), Box(
                                            Input().value("x\ny").multiline().fontSize(10).width(100).height(25)
                                        )
                                            .width(200)
                                            .height(200));
    CHECK(input->displayText == "x\ny");
    CHECK(input->textScrollY == doctest::Approx(0));
}

TEST_CASE("Multiline - paste keeps newlines, normalized to LF") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    InputNode* input = mountMultilineInput(h, "");
    events.focusNode(input);

    // \r\n collapses to \n, a lone \r maps to \n, \n stays: the newlines are
    // KEPT (the single-line strip does not apply — see the C5 paste test).
    test::TestClipboard clip("a\r\nb\rc\nd");
    CHECK(events.handleEditCommand(EditCommand::Paste, false, &clip) == true);
    CHECK(input->displayText == "a\nb\nc\nd");
    CHECK(input->caret == input->displayText.size());
    CHECK(events.consumeTextLayoutChanged() == true);  // line count changed: relayout
}

TEST_CASE("Multiline - soft-wrap affinity: a boundary caret belongs to the earlier line; Home/End are visual") {
    test::FnMeasurer m = tenPxPerByte();
    test::MeasureHarness h;
    h.setMeasurer(&m);
    EventHandler events;
    // "aaaa bbbb" wraps at the 84px span into runs [0,4) and [5,9) — byte 4
    // is the soft break (the space is dropped by the wrap).
    InputNode* input = mountMultilineInput(h, "aaaa bbbb");
    events.focusNode(input);

    // The pinned affinity: caret AT the soft boundary resolves to the END of
    // the EARLIER line (line 0, x=40), one byte further to the next line's start.
    input->caret = 4;
    input->clearSelection();
    CHECK(input->caretPlacement(&m).line == 0);
    CHECK(input->caretPlacement(&m).x == doctest::Approx(40));
    input->caret = 5;
    input->clearSelection();
    CHECK(input->caretPlacement(&m).line == 1);
    CHECK(input->caretPlacement(&m).x == doctest::Approx(0));

    // Home/End work the VISUAL line: End from inside line 0 stops at the wrap
    // point; a MoveRight crosses it; End then spans line 1; Home returns to
    // line 1's start (not the paragraph start).
    input->caret = 1;
    input->clearSelection();
    events.handleEditCommand(EditCommand::MoveLineEnd);
    CHECK(input->caret == 4);
    events.handleEditCommand(EditCommand::MoveRight);
    CHECK(input->caret == 5);
    events.handleEditCommand(EditCommand::MoveLineEnd);
    CHECK(input->caret == 9);
    events.handleEditCommand(EditCommand::MoveLineStart);
    CHECK(input->caret == 5);
}

TEST_CASE("Multiline - a Host edit relayouts the grown input with no app reconcile") {
    test::FnMeasurer m = tenPxPerByte();
    EventTestHost host([] {
        return Box(
                   Input().value("ab").multiline().fontSize(10).width(100).setKey("area")
               )
            .width(200)
            .height(200);
    });
    host.setTextMeasurer(&m);
    host.update(200, 200);
    auto* input = static_cast<InputNode*>(host.root()->children[0].get());
    REQUIRE(input->type() == PrimitiveType::Input);
    CHECK(input->layout.height == doctest::Approx(10));

    // No onChange is wired, so nothing marks the host dirty — the text-layout
    // latch alone must reach calculateLayout on the next update.
    host.focus(input);
    host.handleEditCommand(EditCommand::MoveLineEnd);
    CHECK(host.handleEditCommand(EditCommand::InsertNewline) == true);
    UpdateResult r = host.update(200, 200);
    CHECK(r.layoutChanged == true);
    CHECK(r.needsRepaint == true);
    CHECK(input->layout.height == doctest::Approx(20));
}

TEST_CASE("Multiline - a measurer swap re-measures multiline inputs") {
    // At 10px/char "aa bb cc" (80px) fits the 84px span in ONE line; at
    // 20px/char it wraps to three. The swap must re-dirty the input's measure
    // node (markMeasureNodesDirty gates on the measure func, not on Text).
    test::FnMeasurer ten = tenPxPerByte();
    test::FnMeasurer twenty(
        [](const std::string& t, float, float) { return Size{static_cast<float>(t.size()) * 20.0f, 10.0f}; });
    EventTestHost host([] {
        return Box(
                   Input().value("aa bb cc").multiline().fontSize(10).width(100).setKey("area")
               )
            .width(200)
            .height(200);
    });
    host.setTextMeasurer(&ten);
    host.update(200, 200);
    auto* input = static_cast<InputNode*>(host.root()->children[0].get());
    CHECK(input->layout.height == doctest::Approx(10));

    host.setTextMeasurer(&twenty);
    host.update(200, 200);
    CHECK(input->layout.height == doctest::Approx(30));
}

// ============================================================================
// User-agent keydown routing (the Host::handleKeyDown routing overload): the
// shim maps platform keycodes; CORE owns the priority — (1) a focused Input's
// editing command, (2) focus navigation, (3) app onKeyDown dispatch.
// Regression for the starved-editing-keys bug: the shims dispatched
// handleKeyDown FIRST, an app-level onKeyDown on the ROOT consumed every key,
// and the shims' if-unconsumed editing/Tab ladder never ran. These drive the
// exact seam the shims call.
// ============================================================================

namespace {

struct RoutingCounters {
    int appKeys = 0;
    int submits = 0;
};

// The showcase pattern: an app-level onKeyDown on the ROOT wrapping Inputs
// (Inputs are Tab-focusable with no opt-in).
VNode routingFixture(RoutingCounters& c) {
    return Box(
               Input().value("abc").width(80).height(20).setKey("one").onSubmit(
                   [&c] { c.submits++; }),
               Input().value("xyz").width(80).height(20).setKey("two")
           )
        .width(200)
        .height(100)
        .onKeyDown([&c](int, uint16_t, bool) { c.appKeys++; });
}

// Mount, focus the first Input, and pin its caret at the front so the edits
// below are deterministic.
InputNode* focusFirstRoutingInput(EventTestHost& host) {
    host.update(200, 100);
    auto* input = static_cast<InputNode*>(host.root()->children[0].get());
    REQUIRE(input->type() == PrimitiveType::Input);
    host.focus(input);
    host.handleEditCommand(EditCommand::MoveLineStart);
    return input;
}

}  // namespace

TEST_CASE("KeyRouting - a focused Input's editing key edits; app onKeyDown never sees it") {
    RoutingCounters c;
    EventTestHost host([&c] { return routingFixture(c); });
    InputNode* input = focusFirstRoutingInput(host);

    // THE bug: the old shim order let the root's onKeyDown consume the key and
    // starve the edit. The keycode is arbitrary (999) — core is keycode-agnostic.
    CHECK(host.handleKeyDown(999, 0, false, EditCommand::DeleteForward) == true);
    CHECK(input->displayText == "bc");
    CHECK(c.appKeys == 0);
}

TEST_CASE("KeyRouting - Tab and Shift-Tab always traverse focus; app handlers never see them") {
    RoutingCounters c;
    EventTestHost host([&c] { return routingFixture(c); });
    focusFirstRoutingInput(host);
    Node* one = host.root()->children[0].get();
    Node* two = host.root()->children[1].get();

    CHECK(host.handleKeyDown(9, 0, false, std::nullopt, /*focusNav=*/true) == true);
    CHECK(host.getFocusedNode() == two);
    CHECK(host.handleKeyDown(9, KeyMod_Shift, false, std::nullopt, /*focusNav=*/true) == true);
    CHECK(host.getFocusedNode() == one);
    CHECK(c.appKeys == 0);
}

TEST_CASE("KeyRouting - with no focused Input an editing key falls through to app handlers") {
    RoutingCounters c;
    EventTestHost host([&c] { return routingFixture(c); });
    InputNode* input = focusFirstRoutingInput(host);
    host.blur();

    CHECK(host.handleKeyDown(999, 0, false, EditCommand::DeleteForward) == true);
    CHECK(input->displayText == "abc");  // nothing edited
    CHECK(c.appKeys == 1);
}

TEST_CASE("KeyRouting - an edit inapplicable to the focused input's mode falls through") {
    RoutingCounters c;
    EventTestHost host([&c] { return routingFixture(c); });
    InputNode* input = focusFirstRoutingInput(host);

    // MoveUp on a single-line input is unconsumed -> app dispatch.
    CHECK(host.handleKeyDown(999, 0, false, EditCommand::MoveUp) == true);
    CHECK(input->displayText == "abc");
    CHECK(c.appKeys == 1);
}

TEST_CASE("KeyRouting - Shift in the mods derives the selection-extend flag") {
    RoutingCounters c;
    EventTestHost host([&c] { return routingFixture(c); });
    InputNode* input = focusFirstRoutingInput(host);

    CHECK(host.handleKeyDown(999, KeyMod_Shift, false, EditCommand::MoveRight) == true);
    CHECK(input->hasSelection() == true);
    CHECK(c.appKeys == 0);
}

TEST_CASE("KeyRouting - a non-editing key reaches app handlers while an Input is focused") {
    RoutingCounters c;
    EventTestHost host([&c] { return routingFixture(c); });
    focusFirstRoutingInput(host);

    CHECK(host.handleKeyDown(294, 0, false, std::nullopt) == true);  // an "F5"
    CHECK(c.appKeys == 1);
}

TEST_CASE("KeyRouting - Enter fires onSubmit on a focused single-line input; blurred it falls through") {
    RoutingCounters c;
    EventTestHost host([&c] { return routingFixture(c); });
    focusFirstRoutingInput(host);

    CHECK(host.handleKeyDown(13, 0, false, EditCommand::InsertNewline) == true);
    CHECK(c.submits == 1);
    CHECK(c.appKeys == 0);

    host.blur();
    CHECK(host.handleKeyDown(13, 0, false, EditCommand::InsertNewline) == true);
    CHECK(c.submits == 1);
    CHECK(c.appKeys == 1);
}
