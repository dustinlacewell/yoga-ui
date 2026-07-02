#include "doctest.h"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <yui/core/ComponentContext.hpp>
#include <yui/detail/Reconciler.hpp>

using namespace yui;

TEST_CASE("Mount creates node tree") {
    Reconciler reconciler;

    auto vnode = Box(
        Text("A").setKey("a"),
        Text("B").setKey("b")
    );

    auto fiber = reconciler.mount(std::move(vnode));
    auto* root = reconciler.renderRoot();

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

    auto vnode = Box(
        Text("A"),
        VNode::empty(),
        Text("B")
    );

    auto fiber = reconciler.mount(std::move(vnode));
    auto* root = reconciler.renderRoot();

    CHECK(root->children.size() == 2);
}

TEST_CASE("Reconciler reuses node with same key") {
    Reconciler reconciler;

    auto tree1 = Column(
        Text("A").setKey("a"),
        Text("B").setKey("b")
    );
    auto fiber = reconciler.mount(std::move(tree1));
    auto* root = reconciler.renderRoot();

    auto* nodeA = root->children[0].get();
    auto* nodeB = root->children[1].get();

    // Reorder
    auto tree2 = Column(
        Text("B").setKey("b"),
        Text("A").setKey("a")
    );
    reconciler.reconcile(fiber.get(), std::move(tree2));

    // Same node instances, different positions
    CHECK(root->children.size() == 2);
    CHECK(root->children[0].get() == nodeB);
    CHECK(root->children[1].get() == nodeA);
}

TEST_CASE("Reconciler handles duplicate sibling keys without crashing") {
    Reconciler reconciler;

    // Mount distinct sibling keys.
    auto tree1 = Column(
        Text("A").setKey("dup"),
        Text("B").setKey("other")
    );
    auto fiber = reconciler.mount(std::move(tree1));
    auto* root = reconciler.renderRoot();
    REQUIRE(root != nullptr);
    CHECK(root->children.size() == 2);

    // Re-render where TWO siblings share the same key. The first claims the
    // existing "dup" fiber; without the !reused guard the second would re-claim
    // the now-moved-out fiber and dereference a null slot in find().
    auto tree2 = Column(
        Text("X").setKey("dup"),
        Text("Y").setKey("dup")
    );
    reconciler.reconcile(fiber.get(), std::move(tree2));

    // No crash, and a sane two-child tree: one reused fiber + one fresh fiber.
    REQUIRE(root->children.size() == 2);
    CHECK(root->children[0]->type() == PrimitiveType::Text);
    CHECK(root->children[1]->type() == PrimitiveType::Text);
    CHECK(root->children[0].get() != root->children[1].get());

    auto* text0 = dynamic_cast<TextNode*>(root->children[0].get());
    auto* text1 = dynamic_cast<TextNode*>(root->children[1].get());
    REQUIRE(text0 != nullptr);
    REQUIRE(text1 != nullptr);
    CHECK(text0->props.text == "X");
    CHECK(text1->props.text == "Y");
}

TEST_CASE("Reconciler removes unmatched nodes") {
    Reconciler reconciler;

    auto tree1 = Column(
        Text("A").setKey("a"),
        Text("B").setKey("b")
    );
    auto fiber = reconciler.mount(std::move(tree1));
    auto* root = reconciler.renderRoot();

    auto tree2 = Column(
        Text("A").setKey("a")
    );
    reconciler.reconcile(fiber.get(), std::move(tree2));

    CHECK(root->children.size() == 1);
    CHECK(root->children[0]->key == "a");
}

TEST_CASE("Reconciler reuses+reorders+removes keyed children with clean teardown") {
    // Stresses the reconcileChildren rewrite: a single reconcile that reuses
    // several existing fibers, REORDERS them, and REMOVES others. The previous
    // implementation moved each reused fiber out of the live parentFiber->children
    // mid-loop, leaving transient null holes in the still-observable vector before
    // the final swap. This builds the new ordering without ever holing the live
    // vector; the assertions verify correctness and that subsequent teardown
    // (reconciler destruction below) walks the fiber tree without hitting a hole.
    auto reconciler = std::make_unique<Reconciler>();

    auto tree1 = Column(
        Text("A").setKey("a"),
        Text("B").setKey("b"),
        Text("C").setKey("c"),
        Text("D").setKey("d")
    );
    auto fiber = reconciler->mount(tree1);
    auto* root = reconciler->renderRoot();
    REQUIRE(root->children.size() == 4);

    auto* nodeA = root->children[0].get();
    auto* nodeC = root->children[2].get();
    auto* nodeD = root->children[3].get();

    // Reuse a/c/d, reorder them (d,a,c), drop b, and add a brand-new keyed child.
    auto tree2 = Column(
        Text("D").setKey("d"),
        Text("A").setKey("a"),
        Text("C").setKey("c"),
        Text("E").setKey("e")
    );
    reconciler->reconcile(fiber.get(), tree2);

    REQUIRE(root->children.size() == 4);
    // Reused fibers keep their node identity, now in the new order.
    CHECK(root->children[0].get() == nodeD);
    CHECK(root->children[1].get() == nodeA);
    CHECK(root->children[2].get() == nodeC);
    CHECK(root->children[0]->key == "d");
    CHECK(root->children[1]->key == "a");
    CHECK(root->children[2]->key == "c");
    // The new child is a fresh node.
    CHECK(root->children[3]->key == "e");
    CHECK(root->children[3].get() != nodeA);
    CHECK(root->children[3].get() != nodeC);
    CHECK(root->children[3].get() != nodeD);
    // Removed "b" is gone.
    for (auto& c : root->children) CHECK(c->key != "b");

    // Fiber children mirror the render order and contain no null holes.
    REQUIRE(fiber->children.size() == 4);
    for (auto& f : fiber->children) CHECK(f != nullptr);

    // Destroy everything — exercises willUnmount/~Fiber/~Node teardown over the
    // reconciled tree. A leftover null hole would crash here.
    fiber.reset();
    reconciler.reset();
    CHECK(true);  // reached teardown without crashing
}

TEST_CASE("Reconciler updates props on reused node") {
    Reconciler reconciler;

    auto tree1 = Text("Hello").setKey("t");
    auto fiber = reconciler.mount(std::move(tree1));
    auto* root = reconciler.renderRoot();

    auto* textNode = dynamic_cast<TextNode*>(root);
    REQUIRE(textNode != nullptr);
    CHECK(textNode->props.text == "Hello");

    auto tree2 = Text("World").setKey("t").color(0xFF0000);
    reconciler.reconcile(fiber.get(), std::move(tree2));

    CHECK(textNode->props.text == "World");
    CHECK(textNode->props.color == 0xFF0000);
}

TEST_CASE("Reconciler adds new nodes") {
    Reconciler reconciler;

    auto tree1 = Column(
        Text("A").setKey("a")
    );
    auto fiber = reconciler.mount(std::move(tree1));
    auto* root = reconciler.renderRoot();
    CHECK(root->children.size() == 1);

    auto tree2 = Column(
        Text("A").setKey("a"),
        Text("B").setKey("b")
    );
    reconciler.reconcile(fiber.get(), std::move(tree2));

    CHECK(root->children.size() == 2);
    CHECK(root->children[1]->key == "b");
}

TEST_CASE("Reconciler matches by position when no keys") {
    Reconciler reconciler;

    auto tree1 = Column(
        Text("A"),
        Text("B")
    );
    auto fiber = reconciler.mount(std::move(tree1));
    auto* root = reconciler.renderRoot();

    auto* nodeA = root->children[0].get();
    auto* nodeB = root->children[1].get();

    // Update text but same structure
    auto tree2 = Column(
        Text("A-updated"),
        Text("B-updated")
    );
    reconciler.reconcile(fiber.get(), std::move(tree2));

    // Same node instances (reused by position)
    CHECK(root->children[0].get() == nodeA);
    CHECK(root->children[1].get() == nodeB);

    // Props updated
    auto* textA = dynamic_cast<TextNode*>(root->children[0].get());
    CHECK(textA->props.text == "A-updated");
}

TEST_CASE("Reconciler handles type change") {
    Reconciler reconciler;

    auto tree1 = Column(
        Text("Hello").setKey("item")
    );
    auto fiber = reconciler.mount(std::move(tree1));
    auto* root = reconciler.renderRoot();

    auto* oldNode = root->children[0].get();
    CHECK(oldNode->type() == PrimitiveType::Text);

    // Change from Text to Box with same key
    auto tree2 = Column(
        Box().setKey("item")
    );
    reconciler.reconcile(fiber.get(), std::move(tree2));

    // New node created (different type)
    CHECK(root->children[0].get() != oldNode);
    CHECK(root->children[0]->type() == PrimitiveType::Box);
    CHECK(root->children[0]->key == "item");
}

TEST_CASE("Reconciler handles ROOT type change without crashing") {
    Reconciler reconciler;

    // Root starts as a Box with children.
    auto tree1 = Box(
        Text("inner").setKey("inner")
    );
    auto fiber = reconciler.mount(std::move(tree1));
    auto* root = reconciler.renderRoot();
    REQUIRE(root != nullptr);
    CHECK(root->type() == PrimitiveType::Box);

    // Render function returns a different root primitive next frame. Before the
    // fix this called updateProps with the wrong props variant and threw
    // std::bad_variant_access; now it remounts.
    auto tree2 = Text("x");
    reconciler.reconcile(fiber.get(), std::move(tree2));

    auto* newRoot = reconciler.renderRoot();
    REQUIRE(newRoot != nullptr);
    CHECK(newRoot->type() == PrimitiveType::Text);
    CHECK(fiber->renderNode == newRoot);
    CHECK(fiber->isHost());

    auto* textRoot = dynamic_cast<TextNode*>(newRoot);
    REQUIRE(textRoot != nullptr);
    CHECK(textRoot->props.text == "x");
}

TEST_CASE("Reconciler handles ROOT type change and back, rebuilding children") {
    Reconciler reconciler;

    // NOTE: Column/Row are Box variants, so they share PrimitiveType::Box. To
    // exercise an actual ROOT type change we swap between distinct primitives:
    // Box <-> Scroll (both accept children).
    auto tree1 = Box(
        Text("A").setKey("a"),
        Text("B").setKey("b")
    );
    auto fiber = reconciler.mount(std::move(tree1));
    CHECK(reconciler.renderRoot()->type() == PrimitiveType::Box);
    CHECK(reconciler.renderRoot()->children.size() == 2);

    // Box -> Scroll (different root type): remount.
    auto tree2 = Scroll(std::vector<Child>{
        Text("C").setKey("c"),
    });
    reconciler.reconcile(fiber.get(), std::move(tree2));
    auto* scrollRoot = reconciler.renderRoot();
    REQUIRE(scrollRoot != nullptr);
    CHECK(scrollRoot->type() == PrimitiveType::Scroll);
    REQUIRE(scrollRoot->children.size() == 1);
    CHECK(scrollRoot->children[0]->type() == PrimitiveType::Text);
    CHECK(scrollRoot->children[0]->key == "c");
    CHECK(scrollRoot->children[0]->parent == scrollRoot);
    CHECK(fiber->renderNode == scrollRoot);

    // Scroll -> Box again: remount once more, fiber pointer stays valid.
    auto tree3 = Box(
        Text("D").setKey("d"),
        Text("E").setKey("e")
    );
    reconciler.reconcile(fiber.get(), std::move(tree3));
    auto* boxRoot = reconciler.renderRoot();
    REQUIRE(boxRoot != nullptr);
    CHECK(boxRoot->type() == PrimitiveType::Box);
    CHECK(boxRoot->children.size() == 2);
    CHECK(fiber->renderNode == boxRoot);
    CHECK(fiber->children.size() == 2);

    // Same-type reconcile after a remount still works (props update path).
    auto tree4 = Box(
        Text("D2").setKey("d"),
        Text("E2").setKey("e")
    );
    reconciler.reconcile(fiber.get(), std::move(tree4));
    auto* textD = dynamic_cast<TextNode*>(reconciler.renderRoot()->children[0].get());
    REQUIRE(textD != nullptr);
    CHECK(textD->props.text == "D2");
}

TEST_CASE("Reconciler preserves position matching with empty nodes") {
    Reconciler reconciler;

    auto tree1 = Column(
        Text("A"),
        VNode::empty(),
        Text("B")
    );
    auto fiber = reconciler.mount(std::move(tree1));
    auto* root = reconciler.renderRoot();

    CHECK(root->children.size() == 2);
    auto* nodeA = root->children[0].get();
    auto* nodeB = root->children[1].get();

    auto tree2 = Column(
        Text("A-updated"),
        VNode::empty(),
        Text("B-updated")
    );
    reconciler.reconcile(fiber.get(), std::move(tree2));

    CHECK(root->children.size() == 2);
    CHECK(root->children[0].get() == nodeA);
    CHECK(root->children[1].get() == nodeB);

    auto* textA = dynamic_cast<TextNode*>(root->children[0].get());
    auto* textB = dynamic_cast<TextNode*>(root->children[1].get());
    CHECK(textA->props.text == "A-updated");
    CHECK(textB->props.text == "B-updated");
}

TEST_CASE("Reconciler handles multiple empty nodes") {
    Reconciler reconciler;

    auto tree1 = Column(
        Text("A"),
        VNode::empty(),
        VNode::empty(),
        Text("B"),
        VNode::empty(),
        Text("C")
    );
    auto fiber = reconciler.mount(std::move(tree1));
    auto* root = reconciler.renderRoot();

    CHECK(root->children.size() == 3);
    auto* nodeA = root->children[0].get();
    auto* nodeB = root->children[1].get();
    auto* nodeC = root->children[2].get();

    auto tree2 = Column(
        Text("A2"),
        VNode::empty(),
        VNode::empty(),
        Text("B2"),
        VNode::empty(),
        Text("C2")
    );
    reconciler.reconcile(fiber.get(), std::move(tree2));

    CHECK(root->children[0].get() == nodeA);
    CHECK(root->children[1].get() == nodeB);
    CHECK(root->children[2].get() == nodeC);
}

TEST_CASE("Reconciler handles When condition changing from false to true WITHOUT keys") {
    Reconciler reconciler;

    int createClicked = 0;

    bool showPassword = false;
    auto makeTree = [&]() {
        return Column(
            Text("Before"), Gap(4), When(showPassword, Box()),
            Gap(4), Box().onClick([&]() { createClicked++; })
        );
    };

    auto fiber = reconciler.mount(makeTree());
    auto* root = reconciler.renderRoot();
    CHECK(root->children.size() == 4);

    auto* originalButton = root->children.back().get();
    REQUIRE(originalButton->type() == PrimitiveType::Box);

    auto* boxNode = dynamic_cast<BoxNode*>(originalButton);
    if (boxNode->props.onClick)
        boxNode->props.onClick();
    CHECK(createClicked == 1);

    showPassword = true;
    reconciler.reconcile(fiber.get(), makeTree());

    CHECK(root->children.size() == 5);

    auto* newButton = root->children.back().get();
    REQUIRE(newButton->type() == PrimitiveType::Box);

    CHECK(newButton == originalButton);

    boxNode = dynamic_cast<BoxNode*>(newButton);
    REQUIRE(boxNode != nullptr);
    REQUIRE(boxNode->props.onClick);
    boxNode->props.onClick();
    CHECK(createClicked == 2);
}

TEST_CASE("Reconciler handles When condition changing from true to false") {
    Reconciler reconciler;

    bool showExtra = true;
    auto makeTree = [&]() {
        return Column(
            Text("Before"),
            When(showExtra, Text("Extra content").setKey("extra")),
            Text("After").setKey("after")
        );
    };

    auto fiber = reconciler.mount(makeTree());
    auto* root = reconciler.renderRoot();
    CHECK(root->children.size() == 3);

    Node* afterNode = nullptr;
    for (auto& child : root->children) {
        if (child->key == "after") {
            afterNode = child.get();
            break;
        }
    }
    REQUIRE(afterNode != nullptr);

    showExtra = false;
    reconciler.reconcile(fiber.get(), makeTree());

    CHECK(root->children.size() == 2);

    Node* newAfterNode = nullptr;
    for (auto& child : root->children) {
        if (child->key == "after") {
            newAfterNode = child.get();
            break;
        }
    }
    REQUIRE(newAfterNode != nullptr);
    CHECK(newAfterNode == afterNode);
}

TEST_CASE("Empty to present node gets correct yoga styles") {
    Reconciler reconciler;

    bool showBox = false;
    auto makeTree = [&]() {
        return Column(
            Text("Header"),
            When(showBox, Box(Text("Content")).padding(4)),
            Text("Footer")
        );
    };

    auto fiber = reconciler.mount(makeTree());
    auto* root = reconciler.renderRoot();
    CHECK(root->children.size() == 2);

    showBox = true;
    reconciler.reconcile(fiber.get(), makeTree());

    CHECK(root->children.size() == 3);

    auto* boxNode = dynamic_cast<BoxNode*>(root->children[1].get());
    REQUIRE(boxNode != nullptr);

    float yogaWidth = YGNodeStyleGetWidth(boxNode->yogaNode).value;
    float yogaHeight = YGNodeStyleGetHeight(boxNode->yogaNode).value;
    float yogaPadding = YGNodeStyleGetPadding(boxNode->yogaNode, YGEdgeAll).value;

    CHECK(std::isnan(yogaWidth));
    CHECK(std::isnan(yogaHeight));
    CHECK(yogaPadding == 4.0f);

    root->calculateLayout(200, 200);

    CHECK(boxNode->layout.width > 8.0f);
    CHECK(boxNode->layout.height > 8.0f);
}

TEST_CASE("SessionScreen-like structure: empty->present with nested Gap(4) - NO KEYS") {
    Reconciler reconciler;

    bool showMissingStatus = false;

    auto makeUserRow = [](int userId) {
        return Row(
            Box().width(6).height(6),
            Gap(4),
            Text("User " + std::to_string(userId))
        );
    };

    auto makeTree = [&]() {
        return Column(
            Box(Text("Session")),
            When(showMissingStatus, Box(Text("Missing modules")).padding(4)),
            Scroll(Column(
                makeUserRow(1),
                makeUserRow(2),
                makeUserRow(3)
            )),
            Row(Text("footer"))
        );
    };

    auto fiber = reconciler.mount(makeTree());
    auto* root = reconciler.renderRoot();
    root->calculateLayout(200, 400);

    CHECK(root->children.size() == 3);

    auto* scroll = dynamic_cast<ScrollNode*>(root->children[1].get());
    REQUIRE(scroll != nullptr);
    auto* innerCol = scroll->children[0].get();
    auto* firstRow = innerCol->children[0].get();
    auto* gap = firstRow->children[1].get();
    CHECK(YGNodeStyleGetWidth(gap->yogaNode).value == 4.0f);
    CHECK(YGNodeStyleGetHeight(gap->yogaNode).value == 4.0f);

    showMissingStatus = true;
    reconciler.reconcile(fiber.get(), makeTree());
    root->calculateLayout(200, 400);

    CHECK(root->children.size() == 4);

    auto* missingBox = dynamic_cast<BoxNode*>(root->children[1].get());
    REQUIRE(missingBox != nullptr);

    float yogaWidth = YGNodeStyleGetWidth(missingBox->yogaNode).value;
    float yogaHeight = YGNodeStyleGetHeight(missingBox->yogaNode).value;
    float yogaPadding = YGNodeStyleGetPadding(missingBox->yogaNode, YGEdgeAll).value;

    INFO("MissingBox yoga: width=" << yogaWidth << " height=" << yogaHeight << " padding=" << yogaPadding);
    INFO("MissingBox layout: " << missingBox->layout.width << "x" << missingBox->layout.height);

    CHECK(std::isnan(yogaWidth));
    CHECK(std::isnan(yogaHeight));
    CHECK(yogaPadding == 4.0f);

    CHECK(missingBox->layout.width > 8.0f);
    CHECK(missingBox->layout.height > 8.0f);
}

TEST_CASE("Yoga children are synced") {
    Reconciler reconciler;

    auto tree = Column(
        Box().setKey("a"),
        Box().setKey("b")
    );
    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();

    CHECK(YGNodeGetChildCount(root->yogaNode) == 2);
    CHECK(YGNodeGetChild(root->yogaNode, 0) == root->children[0]->yogaNode);
    CHECK(YGNodeGetChild(root->yogaNode, 1) == root->children[1]->yogaNode);

    auto tree2 = Column(
        Box().setKey("b"),
        Box().setKey("a")
    );
    reconciler.reconcile(fiber.get(), std::move(tree2));

    CHECK(YGNodeGetChild(root->yogaNode, 0) == root->children[0]->yogaNode);
    CHECK(YGNodeGetChild(root->yogaNode, 1) == root->children[1]->yogaNode);
}

TEST_CASE("A throw during a mount pass does not leave the commit queue dangling (UAF)") {
    // Regression for the commit-phase UAF: mountComponent enqueued the component's
    // effect (a raw pointer into the LIVE tree) BEFORE mounting its children. If a
    // child mount threw, the exception unwound and destroyed that fresh component
    // fiber, then propagated PAST the public wrapper's drainCommit() — leaving the
    // freed pointer parked in commit_.effects. The NEXT top-level pass drained it
    // and ran runPendingEffects() on freed memory.
    //
    // Throw seam: the auto-focus callback fires from mountHost while mounting an
    // Input(autoFocus). It runs mid-mount, so throwing from it reproduces the exact
    // "child mount throws" unwind that carried a live-tree raw pointer in the queue.
    Reconciler reconciler;

    bool armed = true;
    reconciler.setAutoFocusCallback([&](InputNode*) {
        if (armed) throw std::runtime_error("autofocus boom");
    });

    auto Comp = [](ComponentContext&) -> VNode { return Input().autoFocus(); };

    // Pass 1: mount throws mid-mount. The wrapper must discard commit_ as it
    // unwinds, so no freed pointer survives.
    CHECK_THROWS_AS(reconciler.mount(Box(Component(Comp))), std::runtime_error);

    // Pass 2: a fresh mount. If pass 1 left the destroyed component fiber in
    // commit_.effects, this drain would call runPendingEffects() on freed memory.
    // Disarm so pass 2 completes cleanly.
    armed = false;
    std::unique_ptr<Fiber> fiber;
    CHECK_NOTHROW(fiber = reconciler.mount(Box(Component(Comp))));
    REQUIRE(fiber != nullptr);
    REQUIRE(reconciler.renderRoot() != nullptr);
    CHECK(reconciler.renderRoot()->type() == PrimitiveType::Box);
}
