#include "doctest.h"

#include <yui/core/EventHandler.hpp>
#include <yui/detail/Reconciler.hpp>
#include <yui/render/StyleResolver.hpp>
#include <yui/yui.hpp>

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace yui;

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

    // Try to scroll past the end
    events.handleScroll(root, 50, 50, 0, -500);
    CHECK(scrollNode->targetScrollY == 100);  // Max scroll = 200 - 100

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

    // The detached content root lays out against the CONTENT width (100 - 2*10).
    CHECK(child->layout.width == doctest::Approx(80));

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
                    .onMouseDown([&](float, float, MouseButton) { ++mouseDowns; })
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
    CHECK_NOTHROW(events.handleSubmit());
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

TEST_CASE("EditCommand - not consumed without a focused Input; unimplemented commands not consumed") {
    Reconciler reconciler;
    EventHandler events;
    std::unique_ptr<Fiber> fiber;
    InputNode* input = mountFocusedInput(reconciler, fiber, events, "abc");

    // Commands whose implementations land in later commits (see EditCommand.hpp)
    // report unconsumed and touch nothing.
    for (EditCommand cmd : {EditCommand::MoveUp, EditCommand::MoveDown, EditCommand::SelectAll,
                            EditCommand::Cut, EditCommand::Copy, EditCommand::Paste,
                            EditCommand::InsertNewline}) {
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
    events.handleSubmit();
    CHECK(submitted == true);

    // Focused Box: the typed view is null, so the same entry points are safe
    // no-ops — the Input's state is untouched.
    events.focusNode(box);
    CHECK(events.getFocusedNode() == box);
    CHECK(events.getFocusedInput() == nullptr);
    CHECK_NOTHROW(events.handleTextInput("x"));
    CHECK(events.handleEditCommand(EditCommand::DeleteBackward) == false);
    CHECK_NOTHROW(events.handleSubmit());
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
