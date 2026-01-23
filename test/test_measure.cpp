#include "doctest.h"

#include <yui/core/Measure.hpp>
#include <yui/core/Reconciler.hpp>

using namespace yui;

TEST_CASE("Text node uses intrinsic size") {
    Reconciler reconciler;

    // Set up a simple measure function for testing
    Measure::setTextMeasure([](const std::string& text, float fontSize, float /*maxWidth*/) -> Size {
        // Simple: 10px per character width, fontSize for height
        return {static_cast<float>(text.length()) * 10.0f, fontSize};
    });

    // Use alignItems FlexStart to prevent stretching
    auto tree = Column({
                           Text("Hello").fontSize(20).setKey("txt"),
                       })
                    .width(200)
                    .alignItems(AlignItems::FlexStart);

    auto root = reconciler.mount(tree);
    root->calculateLayout(200, 100);

    // "Hello" = 5 chars * 10px = 50px width, 20px height
    CHECK(root->children[0]->layout.width == doctest::Approx(50));
    CHECK(root->children[0]->layout.height == doctest::Approx(20));
}

TEST_CASE("Text node respects explicit dimensions") {
    Reconciler reconciler;

    Measure::setTextMeasure([](const std::string& text, float fontSize, float /*maxWidth*/) -> Size {
        return {static_cast<float>(text.length()) * 10.0f, fontSize};
    });

    // Explicit width/height should override intrinsic
    auto tree = Text("Hello").fontSize(20).width(100).height(40);

    auto root = reconciler.mount(tree);
    root->calculateLayout(200, 100);

    CHECK(root->layout.width == doctest::Approx(100));
    CHECK(root->layout.height == doctest::Approx(40));
}

TEST_CASE("Text node with flexGrow expands") {
    Reconciler reconciler;

    Measure::setTextMeasure([](const std::string& text, float fontSize, float /*maxWidth*/) -> Size {
        return {static_cast<float>(text.length()) * 10.0f, fontSize};
    });

    auto tree = Row({
                        Text("Hi").fontSize(16).setKey("a"),
                        Text("World").fontSize(16).flexGrow(1).setKey("b"),
                    })
                    .width(200)
                    .height(50);

    auto root = reconciler.mount(tree);
    root->calculateLayout(200, 50);

    // "Hi" = 2 chars * 10 = 20px intrinsic
    // "World" = 5 chars * 10 = 50px intrinsic, but has flexGrow so expands
    CHECK(root->children[0]->layout.width == doctest::Approx(20));
    CHECK(root->children[1]->layout.width == doctest::Approx(180));  // 200 - 20
}

TEST_CASE("Default fallback measure function") {
    Reconciler reconciler;

    // Clear any custom measure function
    Measure::setTextMeasure(nullptr);

    // Root text with no parent - need to use YGUndefined for available size
    // to let intrinsic sizing work
    auto tree = Text("Test").fontSize(12);

    auto root = reconciler.mount(tree);
    root->calculateLayout(YGUndefined, YGUndefined);

    // Fallback: 0.6 * fontSize per char
    // "Test" = 4 chars * (12 * 0.6) = 4 * 7.2 = 28.8
    // Yoga may apply pixel rounding, so allow tolerance
    CHECK(root->layout.width == doctest::Approx(28.8f).epsilon(0.1));
    CHECK(root->layout.height == doctest::Approx(12));
}

TEST_CASE("Text updates trigger relayout") {
    Reconciler reconciler;

    Measure::setTextMeasure([](const std::string& text, float fontSize, float /*maxWidth*/) -> Size {
        return {static_cast<float>(text.length()) * 10.0f, fontSize};
    });

    auto tree1 = Text("Hi").fontSize(16).setKey("txt");
    auto root = reconciler.mount(tree1);
    root->calculateLayout(YGUndefined, YGUndefined);

    CHECK(root->layout.width == doctest::Approx(20));  // "Hi" = 2 * 10

    // Update text
    auto tree2 = Text("Hello World").fontSize(16).setKey("txt");
    reconciler.reconcile(root.get(), tree2);
    root->calculateLayout(YGUndefined, YGUndefined);

    CHECK(root->layout.width == doctest::Approx(110));  // "Hello World" = 11 * 10
}
