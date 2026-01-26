#include "doctest.h"

#include <yui/core/Reconciler.hpp>

using namespace yui;

TEST_CASE("Mount creates node tree") {
    Reconciler reconciler;

    auto vnode = Box({
        Text("A").setKey("a"),
        Text("B").setKey("b"),
    });

    auto root = reconciler.mount(vnode);

    CHECK(root != nullptr);
    CHECK(root->type() == PrimitiveType::Box);
    CHECK(root->children.size() == 2);
    CHECK(root->children[0]->type() == PrimitiveType::Text);
    CHECK(root->children[0]->key == "a");
    CHECK(root->children[1]->type() == PrimitiveType::Text);
    CHECK(root->children[1]->key == "b");
}

TEST_CASE("Mount skips empty nodes") {
    Reconciler reconciler;

    auto vnode = Box({
        Text("A"),
        VNode::empty(),
        Text("B"),
    });

    auto root = reconciler.mount(vnode);

    CHECK(root->children.size() == 2);
}

TEST_CASE("Reconciler reuses node with same key") {
    Reconciler reconciler;

    auto tree1 = Column({
        Text("A").setKey("a"),
        Text("B").setKey("b"),
    });
    auto root = reconciler.mount(tree1);

    auto* nodeA = root->children[0].get();
    auto* nodeB = root->children[1].get();

    // Reorder
    auto tree2 = Column({
        Text("B").setKey("b"),
        Text("A").setKey("a"),
    });
    reconciler.reconcile(root.get(), tree2);

    // Same node instances, different positions
    CHECK(root->children.size() == 2);
    CHECK(root->children[0].get() == nodeB);
    CHECK(root->children[1].get() == nodeA);
}

TEST_CASE("Reconciler removes unmatched nodes") {
    Reconciler reconciler;

    auto tree1 = Column({
        Text("A").setKey("a"),
        Text("B").setKey("b"),
    });
    auto root = reconciler.mount(tree1);

    // We can't easily swap the node type, so just check child count
    auto tree2 = Column({
        Text("A").setKey("a"),
    });
    reconciler.reconcile(root.get(), tree2);

    CHECK(root->children.size() == 1);
    CHECK(root->children[0]->key == "a");
}

TEST_CASE("Reconciler updates props on reused node") {
    Reconciler reconciler;

    auto tree1 = Text("Hello").setKey("t");
    auto root = reconciler.mount(tree1);

    auto* textNode = dynamic_cast<TextNode*>(root.get());
    REQUIRE(textNode != nullptr);
    CHECK(textNode->props.text == "Hello");

    auto tree2 = Text("World").setKey("t").color(0xFF0000);
    reconciler.reconcile(root.get(), tree2);

    CHECK(textNode->props.text == "World");
    CHECK(textNode->props.color == 0xFF0000);
}

TEST_CASE("Reconciler adds new nodes") {
    Reconciler reconciler;

    auto tree1 = Column({
        Text("A").setKey("a"),
    });
    auto root = reconciler.mount(tree1);
    CHECK(root->children.size() == 1);

    auto tree2 = Column({
        Text("A").setKey("a"),
        Text("B").setKey("b"),
    });
    reconciler.reconcile(root.get(), tree2);

    CHECK(root->children.size() == 2);
    CHECK(root->children[1]->key == "b");
}

TEST_CASE("Reconciler matches by position when no keys") {
    Reconciler reconciler;

    auto tree1 = Column({
        Text("A"),
        Text("B"),
    });
    auto root = reconciler.mount(tree1);

    auto* nodeA = root->children[0].get();
    auto* nodeB = root->children[1].get();

    // Update text but same structure
    auto tree2 = Column({
        Text("A-updated"),
        Text("B-updated"),
    });
    reconciler.reconcile(root.get(), tree2);

    // Same node instances (reused by position)
    CHECK(root->children[0].get() == nodeA);
    CHECK(root->children[1].get() == nodeB);

    // Props updated
    auto* textA = dynamic_cast<TextNode*>(root->children[0].get());
    CHECK(textA->props.text == "A-updated");
}

TEST_CASE("Reconciler handles type change") {
    Reconciler reconciler;

    auto tree1 = Column({
        Text("Hello").setKey("item"),
    });
    auto root = reconciler.mount(tree1);

    auto* oldNode = root->children[0].get();
    CHECK(oldNode->type() == PrimitiveType::Text);

    // Change from Text to Box with same key
    auto tree2 = Column({
        Box().setKey("item"),
    });
    reconciler.reconcile(root.get(), tree2);

    // New node created (different type)
    CHECK(root->children[0].get() != oldNode);
    CHECK(root->children[0]->type() == PrimitiveType::Box);
    CHECK(root->children[0]->key == "item");
}

TEST_CASE("Reconciler preserves position matching with empty nodes") {
    // This tests the bug fix: empty nodes should not affect position matching
    // of subsequent siblings during reconciliation
    Reconciler reconciler;

    // Initial tree with conditional empty in the middle
    auto tree1 = Column({
        Text("A"),
        VNode::empty(),  // When(false, ...)
        Text("B"),
    });
    auto root = reconciler.mount(tree1);

    CHECK(root->children.size() == 2);  // Empty skipped
    auto* nodeA = root->children[0].get();
    auto* nodeB = root->children[1].get();

    // Reconcile with same structure - nodes should be reused
    auto tree2 = Column({
        Text("A-updated"),
        VNode::empty(),
        Text("B-updated"),
    });
    reconciler.reconcile(root.get(), tree2);

    // Same node instances should be reused (position matching worked)
    CHECK(root->children.size() == 2);
    CHECK(root->children[0].get() == nodeA);
    CHECK(root->children[1].get() == nodeB);

    // Props should be updated
    auto* textA = dynamic_cast<TextNode*>(root->children[0].get());
    auto* textB = dynamic_cast<TextNode*>(root->children[1].get());
    CHECK(textA->props.text == "A-updated");
    CHECK(textB->props.text == "B-updated");
}

TEST_CASE("Reconciler handles multiple empty nodes") {
    Reconciler reconciler;

    auto tree1 = Column({
        Text("A"),
        VNode::empty(),
        VNode::empty(),
        Text("B"),
        VNode::empty(),
        Text("C"),
    });
    auto root = reconciler.mount(tree1);

    CHECK(root->children.size() == 3);
    auto* nodeA = root->children[0].get();
    auto* nodeB = root->children[1].get();
    auto* nodeC = root->children[2].get();

    // Reconcile
    auto tree2 = Column({
        Text("A2"),
        VNode::empty(),
        VNode::empty(),
        Text("B2"),
        VNode::empty(),
        Text("C2"),
    });
    reconciler.reconcile(root.get(), tree2);

    // All nodes reused
    CHECK(root->children[0].get() == nodeA);
    CHECK(root->children[1].get() == nodeB);
    CHECK(root->children[2].get() == nodeC);
}

TEST_CASE("Reconciler handles When condition changing from false to true WITHOUT keys") {
    // This reproduces the HostOverlay bug: clicking "Private" makes the password
    // field appear (When goes from empty to content), which breaks Create/Cancel buttons.
    //
    // The structure:
    //   VNode positions (isPublic=true):  [Text, Gap, When(empty), Gap, Row]
    //   Realized nodes:                   [Text, Gap, Gap, Row]
    //
    //   VNode positions (isPublic=false): [Text, Gap, When(Column), Gap, Row]
    //   Realized nodes should be:         [Text, Gap, Column, Gap, Row]
    //
    // The bug: when When changes from empty to content, the reconciler's position
    // counter gets out of sync because it increments for new realized nodes but
    // tries to match against old positions that don't have the new node.
    Reconciler reconciler;

    // Track onClick to verify the right handlers end up on the right buttons
    int createClicked = 0;
    int cancelClicked = 0;

    bool showPassword = false;
    auto makeTree = [&]() {
        return Column({
            Text("Before"), Gap(4), When(showPassword, Box()),  // Was empty, becomes content (NO KEY)
            Gap(4), Box().onClick([&]() { createClicked++; }),  // This button should keep working
        });
    };

    auto root = reconciler.mount(makeTree());
    CHECK(root->children.size() == 4);  // Text, Gap, Gap, Box (no password box)

    // Capture the original button pointer
    auto* originalButton = root->children.back().get();
    REQUIRE(originalButton->type() == PrimitiveType::Box);

    // Click works
    auto* boxNode = dynamic_cast<BoxNode*>(originalButton);
    if (boxNode->props.onClick)
        boxNode->props.onClick();
    CHECK(createClicked == 1);

    // Toggle: empty -> content
    showPassword = true;
    reconciler.reconcile(root.get(), makeTree());

    // Now should have 5 children
    CHECK(root->children.size() == 5);  // Text, Gap, Box(password), Gap, Box(button)

    // The button should still work - it's the LAST child
    auto* newButton = root->children.back().get();
    REQUIRE(newButton->type() == PrimitiveType::Box);

    // The button should be the SAME NODE (reused) - this is the key test!
    // If reconciliation is broken, a new node is created and the pointer differs
    CHECK(newButton == originalButton);  // CRITICAL: same node instance means correct reuse

    boxNode = dynamic_cast<BoxNode*>(newButton);
    REQUIRE(boxNode != nullptr);
    REQUIRE(boxNode->props.onClick);  // This is the failing assertion if props get mismatched
    boxNode->props.onClick();
    CHECK(createClicked == 2);  // Should increment
}

TEST_CASE("Reconciler handles When condition changing from true to false") {
    Reconciler reconciler;

    bool showExtra = true;
    auto makeTree = [&]() {
        return Column({
            Text("Before"),
            When(showExtra, Text("Extra content").setKey("extra")),
            Text("After").setKey("after"),
        });
    };

    auto root = reconciler.mount(makeTree());
    CHECK(root->children.size() == 3);  // Before, Extra, After

    // Find the "after" node
    Node* afterNode = nullptr;
    for (auto& child : root->children) {
        if (child->key == "after") {
            afterNode = child.get();
            break;
        }
    }
    REQUIRE(afterNode != nullptr);

    // Toggle off
    showExtra = false;
    reconciler.reconcile(root.get(), makeTree());

    CHECK(root->children.size() == 2);  // Before, After (Extra removed)

    // The "after" node should still be reused
    Node* newAfterNode = nullptr;
    for (auto& child : root->children) {
        if (child->key == "after") {
            newAfterNode = child.get();
            break;
        }
    }
    REQUIRE(newAfterNode != nullptr);
    CHECK(newAfterNode == afterNode);  // Same instance
}

TEST_CASE("Empty to present node gets correct yoga styles") {
    // This tests the bug where a node going from VNode::empty() to present
    // would get incorrect yoga styles (e.g., width/height instead of padding)
    Reconciler reconciler;

    bool showBox = false;
    auto makeTree = [&]() {
        return Column({
            Text("Header"),
            When(showBox, Box(Text("Content")).padding(4)),  // No width/height, just padding
            Text("Footer"),
        });
    };

    auto root = reconciler.mount(makeTree());
    CHECK(root->children.size() == 2);  // Header, Footer (box hidden)

    // Show the box
    showBox = true;
    reconciler.reconcile(root.get(), makeTree());

    CHECK(root->children.size() == 3);  // Header, Box, Footer

    // The middle child should be the Box
    auto* boxNode = dynamic_cast<BoxNode*>(root->children[1].get());
    REQUIRE(boxNode != nullptr);

    // Check yoga styles - THIS IS THE KEY TEST
    // The box should have padding=4 but NO explicit width/height
    float yogaWidth = YGNodeStyleGetWidth(boxNode->yogaNode).value;
    float yogaHeight = YGNodeStyleGetHeight(boxNode->yogaNode).value;
    float yogaPadding = YGNodeStyleGetPadding(boxNode->yogaNode, YGEdgeAll).value;

    // Width and height should be undefined (NaN or auto)
    // Yoga uses NaN for undefined values
    CHECK(std::isnan(yogaWidth));  // Should NOT be 4
    CHECK(std::isnan(yogaHeight)); // Should NOT be 4
    CHECK(yogaPadding == 4.0f);    // Should be 4

    // Calculate layout to verify the box sizes to content
    root->calculateLayout(200, 200);

    // Box should be larger than 8x8 (which would happen if width=4, height=4 + padding=4)
    // With padding=4 and text content, it should be at least text_width+8 x text_height+8
    CHECK(boxNode->layout.width > 8.0f);
    CHECK(boxNode->layout.height > 8.0f);
}

TEST_CASE("SessionScreen-like structure: empty->present with nested Gap(4) - NO KEYS") {
    // This mimics the actual SessionScreen structure that had the bug:
    // - Column with Header, conditional status, Scroll with nested content, Footer
    // - The Scroll contains nested Rows with Gap(4) components
    // - ALL NODES ARE KEYLESS (before the fix)
    Reconciler reconciler;

    bool showMissingStatus = false;

    auto makeUserRow = [](int userId) {
        return Row({
            Box().width(6).height(6),  // StatusDot
            Gap(4),                     // THE PROBLEMATIC Gap(4)
            Text("User " + std::to_string(userId)),
        });
    };

    auto makeTree = [&]() {
        return Column({
            // Header - NO KEY
            Box(Text("Session")),

            // Missing status - conditional, NO KEY
            When(showMissingStatus, Box(Text("Missing modules")).padding(4)),

            // Scroll with nested content containing Gap(4) - NO KEY
            Scroll(Column({
                makeUserRow(1),
                makeUserRow(2),
                makeUserRow(3),
            })),

            // Footer - NO KEY
            Row({Text("footer")}),
        });
    };

    // First render - status hidden
    auto root = reconciler.mount(makeTree());
    root->calculateLayout(200, 400);

    CHECK(root->children.size() == 3);  // Header, Scroll, Footer

    // Verify Gap(4) exists deep in tree with width=4, height=4
    auto* scroll = dynamic_cast<ScrollNode*>(root->children[1].get());
    REQUIRE(scroll != nullptr);
    auto* innerCol = scroll->children[0].get();
    auto* firstRow = innerCol->children[0].get();
    auto* gap = firstRow->children[1].get();  // Gap is second child of Row
    CHECK(YGNodeStyleGetWidth(gap->yogaNode).value == 4.0f);
    CHECK(YGNodeStyleGetHeight(gap->yogaNode).value == 4.0f);

    // Second render - status shown
    showMissingStatus = true;
    reconciler.reconcile(root.get(), makeTree());
    root->calculateLayout(200, 400);

    CHECK(root->children.size() == 4);  // Header, MissingStatus, Scroll, Footer

    // THE CRITICAL CHECK: The missing status box should have correct styles
    auto* missingBox = dynamic_cast<BoxNode*>(root->children[1].get());
    REQUIRE(missingBox != nullptr);

    float yogaWidth = YGNodeStyleGetWidth(missingBox->yogaNode).value;
    float yogaHeight = YGNodeStyleGetHeight(missingBox->yogaNode).value;
    float yogaPadding = YGNodeStyleGetPadding(missingBox->yogaNode, YGEdgeAll).value;

    INFO("MissingBox yoga: width=" << yogaWidth << " height=" << yogaHeight << " padding=" << yogaPadding);
    INFO("MissingBox layout: " << missingBox->layout.width << "x" << missingBox->layout.height);

    // Width/height should be undefined (NaN), padding should be 4
    CHECK(std::isnan(yogaWidth));   // FAILS if bug: would be 4 from Gap(4)
    CHECK(std::isnan(yogaHeight));  // FAILS if bug: would be 4 from Gap(4)
    CHECK(yogaPadding == 4.0f);

    // Layout should be larger than 8x8
    CHECK(missingBox->layout.width > 8.0f);
    CHECK(missingBox->layout.height > 8.0f);
}

TEST_CASE("Yoga children are synced") {
    Reconciler reconciler;

    auto tree = Column({
        Box().setKey("a"),
        Box().setKey("b"),
    });
    auto root = reconciler.mount(tree);

    // Check yoga tree matches
    CHECK(YGNodeGetChildCount(root->yogaNode) == 2);
    CHECK(YGNodeGetChild(root->yogaNode, 0) == root->children[0]->yogaNode);
    CHECK(YGNodeGetChild(root->yogaNode, 1) == root->children[1]->yogaNode);

    // Reorder
    auto tree2 = Column({
        Box().setKey("b"),
        Box().setKey("a"),
    });
    reconciler.reconcile(root.get(), tree2);

    // Yoga tree updated
    CHECK(YGNodeGetChild(root->yogaNode, 0) == root->children[0]->yogaNode);
    CHECK(YGNodeGetChild(root->yogaNode, 1) == root->children[1]->yogaNode);
}
