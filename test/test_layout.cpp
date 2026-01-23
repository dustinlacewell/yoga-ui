#include "doctest.h"

#include <yui/core/Reconciler.hpp>

using namespace yui;

TEST_CASE("Basic layout calculation") {
    Reconciler reconciler;

    auto tree = Box().width(100).height(50);
    auto root = reconciler.mount(tree);

    root->calculateLayout(100, 50);

    CHECK(root->layout.width == doctest::Approx(100));
    CHECK(root->layout.height == doctest::Approx(50));
    CHECK(root->layout.left == doctest::Approx(0));
    CHECK(root->layout.top == doctest::Approx(0));
}

TEST_CASE("flexGrow distributes space") {
    Reconciler reconciler;

    auto tree = Row({
                        Box().flexGrow(1).setKey("a"),
                        Box().flexGrow(2).setKey("b"),
                    })
                    .width(300)
                    .height(100);

    auto root = reconciler.mount(tree);
    root->calculateLayout(300, 100);

    CHECK(root->children[0]->layout.width == doctest::Approx(100));
    CHECK(root->children[1]->layout.width == doctest::Approx(200));
}

TEST_CASE("padding affects child position") {
    Reconciler reconciler;

    auto tree = Box({
                        Box().setKey("child"),
                    })
                    .padding(10)
                    .width(100)
                    .height(100);

    auto root = reconciler.mount(tree);
    root->calculateLayout(100, 100);

    CHECK(root->children[0]->layout.left == doctest::Approx(10));
    CHECK(root->children[0]->layout.top == doctest::Approx(10));
}

TEST_CASE("Column layout stacks vertically") {
    Reconciler reconciler;

    auto tree = Column({
                           Box().height(20).setKey("a"),
                           Box().height(30).setKey("b"),
                           Box().height(40).setKey("c"),
                       })
                    .width(100);

    auto root = reconciler.mount(tree);
    root->calculateLayout(100, 200);

    CHECK(root->children[0]->layout.top == doctest::Approx(0));
    CHECK(root->children[1]->layout.top == doctest::Approx(20));
    CHECK(root->children[2]->layout.top == doctest::Approx(50));
}

TEST_CASE("Row layout stacks horizontally") {
    Reconciler reconciler;

    auto tree = Row({
                        Box().width(20).setKey("a"),
                        Box().width(30).setKey("b"),
                        Box().width(40).setKey("c"),
                    })
                    .height(100);

    auto root = reconciler.mount(tree);
    root->calculateLayout(200, 100);

    CHECK(root->children[0]->layout.left == doctest::Approx(0));
    CHECK(root->children[1]->layout.left == doctest::Approx(20));
    CHECK(root->children[2]->layout.left == doctest::Approx(50));
}

TEST_CASE("gap adds space between children") {
    Reconciler reconciler;

    auto tree = Column({
                           Box().height(20).setKey("a"),
                           Box().height(20).setKey("b"),
                           Box().height(20).setKey("c"),
                       })
                    .gap(10)
                    .width(100);

    auto root = reconciler.mount(tree);
    root->calculateLayout(100, 200);

    CHECK(root->children[0]->layout.top == doctest::Approx(0));
    CHECK(root->children[1]->layout.top == doctest::Approx(30));  // 20 + 10 gap
    CHECK(root->children[2]->layout.top == doctest::Approx(60));  // 20 + 10 + 20 + 10
}

TEST_CASE("justifyContent center") {
    Reconciler reconciler;

    auto tree = Row({
                        Box().width(50).setKey("child"),
                    })
                    .width(200)
                    .height(100)
                    .justifyContent(JustifyContent::Center);

    auto root = reconciler.mount(tree);
    root->calculateLayout(200, 100);

    CHECK(root->children[0]->layout.left == doctest::Approx(75));  // (200 - 50) / 2
}

TEST_CASE("alignItems center") {
    Reconciler reconciler;

    auto tree = Row({
                        Box().width(50).height(30).setKey("child"),
                    })
                    .width(200)
                    .height(100)
                    .alignItems(AlignItems::Center);

    auto root = reconciler.mount(tree);
    root->calculateLayout(200, 100);

    CHECK(root->children[0]->layout.top == doctest::Approx(35));  // (100 - 30) / 2
}

TEST_CASE("nested layout") {
    Reconciler reconciler;

    auto tree = Column({
                           Row({
                                   Box().flexGrow(1).setKey("a1"),
                                   Box().flexGrow(1).setKey("a2"),
                               })
                               .height(50)
                               .setKey("row1"),
                           Row({
                                   Box().width(30).setKey("b1"),
                                   Box().flexGrow(1).setKey("b2"),
                               })
                               .height(50)
                               .setKey("row2"),
                       })
                    .width(100);

    auto root = reconciler.mount(tree);
    root->calculateLayout(100, 100);

    // First row: two equal flex children
    auto* row1 = root->children[0].get();
    CHECK(row1->children[0]->layout.width == doctest::Approx(50));
    CHECK(row1->children[1]->layout.width == doctest::Approx(50));

    // Second row: fixed + flex
    auto* row2 = root->children[1].get();
    CHECK(row2->children[0]->layout.width == doctest::Approx(30));
    CHECK(row2->children[1]->layout.width == doctest::Approx(70));
}

TEST_CASE("margin adds space outside element") {
    Reconciler reconciler;

    auto tree = Box({
                        Box().width(50).height(50).margin(10).setKey("child"),
                    })
                    .width(100)
                    .height(100);

    auto root = reconciler.mount(tree);
    root->calculateLayout(100, 100);

    CHECK(root->children[0]->layout.left == doctest::Approx(10));
    CHECK(root->children[0]->layout.top == doctest::Approx(10));
}

TEST_CASE("layout persists after reconciliation") {
    Reconciler reconciler;

    auto tree1 = Row({
                         Box().flexGrow(1).setKey("a"),
                         Box().flexGrow(1).setKey("b"),
                     })
                     .width(200)
                     .height(100);

    auto root = reconciler.mount(tree1);
    root->calculateLayout(200, 100);

    CHECK(root->children[0]->layout.width == doctest::Approx(100));

    // Reconcile with different flex values
    auto tree2 = Row({
                         Box().flexGrow(1).setKey("a"),
                         Box().flexGrow(3).setKey("b"),
                     })
                     .width(200)
                     .height(100);

    reconciler.reconcile(root.get(), tree2);
    root->calculateLayout(200, 100);

    CHECK(root->children[0]->layout.width == doctest::Approx(50));
    CHECK(root->children[1]->layout.width == doctest::Approx(150));
}
