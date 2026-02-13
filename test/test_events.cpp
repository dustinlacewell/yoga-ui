#include "doctest.h"

#include <yui/core/EventHandler.hpp>
#include <yui/core/Reconciler.hpp>

using namespace yui;

TEST_CASE("Hit test finds deepest node") {
    Reconciler reconciler;
    EventHandler events;

    // Nested boxes
    auto tree = Box({
                        Box().width(50).height(50).setKey("inner"),
                    })
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(tree);
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

    auto fiber = reconciler.mount(tree);
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

    auto fiber = reconciler.mount(tree);
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    events.handleMouseUp(root, 50, 50, MouseButton::Right);
    CHECK(rightClicked);
}

TEST_CASE("Click bubbles to parent") {
    Reconciler reconciler;
    EventHandler events;

    bool parentClicked = false;
    auto tree = Box({
                        Box().width(50).height(50).setKey("inner"),
                    })
                    .width(100)
                    .height(100)
                    .onClick([&]() { parentClicked = true; });

    auto fiber = reconciler.mount(tree);
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    // Click on inner (which has no handler)
    events.handleMouseUp(root, 25, 25, MouseButton::Left);

    // Should bubble to parent
    CHECK(parentClicked);
}

TEST_CASE("Click consumption stops bubbling") {
    Reconciler reconciler;
    EventHandler events;

    bool innerClicked = false;
    bool outerClicked = false;

    auto tree = Box({
                        Box().width(50).height(50).setKey("inner").onClick([&]() { innerClicked = true; }),
                    })
                    .width(100)
                    .height(100)
                    .onClick([&]() { outerClicked = true; });

    auto fiber = reconciler.mount(tree);
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    // Click on inner
    events.handleMouseUp(root, 25, 25, MouseButton::Left);

    CHECK(innerClicked);
    CHECK(!outerClicked);  // Stopped by inner handler
}

TEST_CASE("Hover callbacks fire") {
    Reconciler reconciler;
    EventHandler events;

    bool hovered = false;
    auto tree = Box().width(100).height(100).onHover([&](bool h) { hovered = h; });

    auto fiber = reconciler.mount(tree);
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

    auto tree = Box({
                        Box().width(50).height(50).setKey("inner").onHover([&](bool h) { innerHovered = h; }),
                    })
                    .width(100)
                    .height(100)
                    .onHover([&](bool h) { outerHovered = h; });

    auto fiber = reconciler.mount(tree);
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

TEST_CASE("Text node receives events") {
    Reconciler reconciler;
    EventHandler events;

    bool clicked = false;
    auto tree = Text("Click me").width(100).height(20).onClick([&]() { clicked = true; });

    auto fiber = reconciler.mount(tree);
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 20);

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

    auto fiber = reconciler.mount(tree);
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
    auto tree = Box({
                        Box().width(50).height(50).setKey("inner"),
                    })
                    .width(100)
                    .height(100)
                    .onScroll([&](float dx, float dy) { parentScrollY = dy; });

    auto fiber = reconciler.mount(tree);
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

    auto fiber = reconciler.mount(tree);
    auto* root = reconciler.renderRoot();
    REQUIRE(root != nullptr);
    CHECK(root->type() == PrimitiveType::Scroll);
}

TEST_CASE("ScrollNode layout works") {
    Reconciler reconciler;

    auto tree = Scroll(Box().width(200).height(300)).width(100).height(100);

    auto fiber = reconciler.mount(tree);
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

    auto fiber = reconciler.mount(tree);
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

    auto fiber = reconciler.mount(tree);
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

    auto fiber = reconciler.mount(tree);
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

    auto fiber = reconciler.mount(tree);
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
    auto tree = Scroll({
                           Box().width(100).height(200).onClick([&]() { childClicked = true; }),
                       })
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(tree);
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    auto* scrollNode = static_cast<ScrollNode*>(root);

    // Click at y=50 should hit child initially
    events.handleMouseUp(root, 50, 50, MouseButton::Left);
    CHECK(childClicked);

    childClicked = false;

    // Scroll down by 50px
    scrollNode->scrollOffsetY = 50;

    // Click at y=50 now hits y=100 in content (still in child's 0-200 range)
    events.handleMouseUp(root, 50, 50, MouseButton::Left);
    CHECK(childClicked);
}

TEST_CASE("ScrollNode child layout dimensions - fixed size child") {
    Reconciler reconciler;

    // Child with explicit dimensions larger than parent
    auto tree = Scroll(Box().width(200).height(300)).width(100).height(100);

    auto fiber = reconciler.mount(tree);
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
    auto tree = Scroll(Box({
                               Box().height(50).setKey("item1"),
                               Box().height(50).setKey("item2"),
                               Box().height(50).setKey("item3"),
                               Box().height(50).setKey("item4"),
                               Box().height(50).setKey("item5"),
                               Box().height(50).setKey("item6"),
                           })
                           .flexDirection(FlexDirection::Column))
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(tree);
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

    auto fiber = reconciler.mount(tree);
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

    // This mimics SessionScreen: Scroll(Column({sections...}).gap(6)).flexGrow(1)
    // The Column has NO explicit height - it should size to fit its children
    auto tree = Scroll(Column({
                                  // Section 1: header + 2 items
                                  Column({
                                             Text("SECTION 1").height(14),
                                             Box().height(20).setKey("item1a"),
                                             Box().height(20).setKey("item1b"),
                                         })
                                      .gap(2)
                                      .setKey("section1"),
                                  // Section 2: header + 3 items
                                  Column({
                                             Text("SECTION 2").height(14),
                                             Box().height(20).setKey("item2a"),
                                             Box().height(20).setKey("item2b"),
                                             Box().height(20).setKey("item2c"),
                                         })
                                      .gap(2)
                                      .setKey("section2"),
                                  // Section 3: header + 5 items
                                  Column({
                                             Text("SECTION 3").height(14),
                                             Box().height(20).setKey("item3a"),
                                             Box().height(20).setKey("item3b"),
                                             Box().height(20).setKey("item3c"),
                                             Box().height(20).setKey("item3d"),
                                             Box().height(20).setKey("item3e"),
                                         })
                                      .gap(2)
                                      .setKey("section3"),
                              })
                           .gap(6)  // NO explicit height on the Column!
                       )
                    .width(100)
                    .height(100)
                    .flexGrow(1);

    auto fiber = reconciler.mount(tree);
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
    // Column({
    //     Header stuff...
    //     renderUserList()  <- returns Scroll(...).flexGrow(1)
    //     Footer stuff...
    // }).flexGrow(1).padding(4).gap(4)
    // Build a list with enough items to exceed the scroll container
    std::vector<Child> items;
    for (int i = 0; i < 20; i++) {
        items.push_back(Box().height(30).setKey("section" + std::to_string(i)));
    }

    auto tree = Column({
                           // Header
                           Box().height(20).setKey("header"),
                           // User list - Scroll with flexGrow(1) containing Column with many items
                           Scroll(Column(std::move(items)).gap(6)  // 20 items * 30 + 19 gaps * 6 = 600 + 114 = 714px
                                  )
                               .flexGrow(1)
                               .flexShrink(1)
                               .setKey("scroll"),
                           // Footer
                           Box().height(20).setKey("footer"),
                       })
                    .flexGrow(1)
                    .padding(4)
                    .gap(4);

    auto fiber = reconciler.mount(tree);
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
