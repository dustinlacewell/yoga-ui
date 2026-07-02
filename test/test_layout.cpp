#include "doctest.h"

#include "TestMeasurer.hpp"

#include <yui/detail/Reconciler.hpp>

using namespace yui;
using yui::test::FnMeasurer;
using yui::test::MeasureHarness;

namespace {

// Canonical test measurer: 10px per character, fontSize for height.
FnMeasurer::Fn perChar10() {
    return [](const std::string& text, float fontSize, float /*maxWidth*/) -> Size {
        return {static_cast<float>(text.length()) * 10.0f, fontSize};
    };
}

}  // namespace

TEST_CASE("Basic layout calculation") {
    Reconciler reconciler;

    auto tree = Box().width(100).height(50);
    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();

    root->calculateLayout(100, 50);

    CHECK(root->layout.width == doctest::Approx(100));
    CHECK(root->layout.height == doctest::Approx(50));
    CHECK(root->layout.left == doctest::Approx(0));
    CHECK(root->layout.top == doctest::Approx(0));
}

TEST_CASE("flexGrow distributes space") {
    Reconciler reconciler;

    auto tree = Row(
                        Box().flexGrow(1).setKey("a"),
                        Box().flexGrow(2).setKey("b")
                    )
                    .width(300)
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(300, 100);

    CHECK(root->children[0]->layout.width == doctest::Approx(100));
    CHECK(root->children[1]->layout.width == doctest::Approx(200));
}

TEST_CASE("padding affects child position") {
    Reconciler reconciler;

    auto tree = Box(
                        Box().setKey("child")
                    )
                    .padding(10)
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    CHECK(root->children[0]->layout.left == doctest::Approx(10));
    CHECK(root->children[0]->layout.top == doctest::Approx(10));
}

TEST_CASE("Column layout stacks vertically") {
    Reconciler reconciler;

    auto tree = Column(
                           Box().height(20).setKey("a"),
                           Box().height(30).setKey("b"),
                           Box().height(40).setKey("c")
                       )
                    .width(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 200);

    CHECK(root->children[0]->layout.top == doctest::Approx(0));
    CHECK(root->children[1]->layout.top == doctest::Approx(20));
    CHECK(root->children[2]->layout.top == doctest::Approx(50));
}

TEST_CASE("Row layout stacks horizontally") {
    Reconciler reconciler;

    auto tree = Row(
                        Box().width(20).setKey("a"),
                        Box().width(30).setKey("b"),
                        Box().width(40).setKey("c")
                    )
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(200, 100);

    CHECK(root->children[0]->layout.left == doctest::Approx(0));
    CHECK(root->children[1]->layout.left == doctest::Approx(20));
    CHECK(root->children[2]->layout.left == doctest::Approx(50));
}

TEST_CASE("gap adds space between children") {
    Reconciler reconciler;

    auto tree = Column(
                           Box().height(20).setKey("a"),
                           Box().height(20).setKey("b"),
                           Box().height(20).setKey("c")
                       )
                    .gap(10)
                    .width(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 200);

    CHECK(root->children[0]->layout.top == doctest::Approx(0));
    CHECK(root->children[1]->layout.top == doctest::Approx(30));  // 20 + 10 gap
    CHECK(root->children[2]->layout.top == doctest::Approx(60));  // 20 + 10 + 20 + 10
}

TEST_CASE("justifyContent center") {
    Reconciler reconciler;

    auto tree = Row(
                        Box().width(50).setKey("child")
                    )
                    .width(200)
                    .height(100)
                    .justifyContent(JustifyContent::Center);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(200, 100);

    CHECK(root->children[0]->layout.left == doctest::Approx(75));  // (200 - 50) / 2
}

TEST_CASE("alignItems center") {
    Reconciler reconciler;

    auto tree = Row(
                        Box().width(50).height(30).setKey("child")
                    )
                    .width(200)
                    .height(100)
                    .alignItems(AlignItems::Center);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(200, 100);

    CHECK(root->children[0]->layout.top == doctest::Approx(35));  // (100 - 30) / 2
}

TEST_CASE("nested layout") {
    Reconciler reconciler;

    auto tree = Column(
                           Row(
                                   Box().flexGrow(1).setKey("a1"),
                                   Box().flexGrow(1).setKey("a2")
                               )
                               .height(50)
                               .setKey("row1"),
                           Row(
                                   Box().width(30).setKey("b1"),
                                   Box().flexGrow(1).setKey("b2")
                               )
                               .height(50)
                               .setKey("row2")
                       )
                    .width(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
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

    auto tree = Box(
                        Box().width(50).height(50).margin(10).setKey("child")
                    )
                    .width(100)
                    .height(100);

    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    CHECK(root->children[0]->layout.left == doctest::Approx(10));
    CHECK(root->children[0]->layout.top == doctest::Approx(10));
}

TEST_CASE("layout persists after reconciliation") {
    Reconciler reconciler;

    auto tree1 = Row(
                         Box().flexGrow(1).setKey("a"),
                         Box().flexGrow(1).setKey("b")
                     )
                     .width(200)
                     .height(100);

    auto fiber = reconciler.mount(std::move(tree1));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(200, 100);

    CHECK(root->children[0]->layout.width == doctest::Approx(100));

    // Reconcile with different flex values
    auto tree2 = Row(
                         Box().flexGrow(1).setKey("a"),
                         Box().flexGrow(3).setKey("b")
                     )
                     .width(200)
                     .height(100);

    reconciler.reconcile(fiber.get(), std::move(tree2));
    root = reconciler.renderRoot();
    root->calculateLayout(200, 100);

    CHECK(root->children[0]->layout.width == doctest::Approx(50));
    CHECK(root->children[1]->layout.width == doctest::Approx(150));
}

TEST_CASE("Box wraps Text child (intrinsic sizing)") {
    FnMeasurer measurer(perChar10());
    MeasureHarness h;
    h.setMeasurer(&measurer);

    // Box wrapping Text with no explicit dimensions - should inherit child's size
    // This is the pattern: Box(Label(text)).backgroundColor(...)
    auto tree = Box(Text("Hello").fontSize(16).setKey("txt"))
        .justifyContent(JustifyContent::Center)
        .alignItems(AlignItems::Center)
        .setKey("box");

    auto* root = h.mount(std::move(tree));
    root->calculateLayout(YGUndefined, YGUndefined);

    // "Hello" = 5 chars * 10px = 50px width, 16px height
    // Box should wrap to fit the text
    CHECK(root->layout.width == doctest::Approx(50));
    CHECK(root->layout.height == doctest::Approx(16));
}

TEST_CASE("Box wraps Text child in Column with FlexStart") {
    FnMeasurer measurer(perChar10());
    MeasureHarness h;
    h.setMeasurer(&measurer);

    // Box inside Column with alignItems FlexStart (prevents stretch)
    auto tree = Column(
        Box(Text("Hello").fontSize(16).setKey("txt"))
            .justifyContent(JustifyContent::Center)
            .alignItems(AlignItems::Center)
            .setKey("box")
    ).width(200).alignItems(AlignItems::FlexStart);

    auto* root = h.mount(std::move(tree));
    root->calculateLayout(200, YGUndefined);

    // Box should wrap text, not stretch to column width
    CHECK(root->children[0]->layout.width == doctest::Approx(50));
    CHECK(root->children[0]->layout.height == doctest::Approx(16));
}

TEST_CASE("Box wraps Text in Column - real app scenario") {
    FnMeasurer measurer(perChar10());
    MeasureHarness h;
    h.setMeasurer(&measurer);

    // Mimics the SessionScreen layout:
    // Column with fixed dimensions, Box header wrapping Text
    auto tree = Column(
        // Header Box - no explicit size, should wrap text HEIGHT
        Box(Text("Session Name").fontSize(12).setKey("txt"))
            .backgroundColor(0x202020FF)
            .justifyContent(JustifyContent::Center)
            .alignItems(AlignItems::Center)
            .setKey("header"),
        // Other content
        Box().flexGrow(1).setKey("content")
    )
    .width(180)
    .height(480)
    .padding(4)
    .gap(4);

    auto* root = h.mount(std::move(tree));
    root->calculateLayout(180, 480);

    auto* header = root->children[0].get();
    auto* headerText = header->children[0].get();

    MESSAGE("Header Box layout: " << header->layout.width << "x" << header->layout.height);
    MESSAGE("Header Text layout: " << headerText->layout.width << "x" << headerText->layout.height);

    // "Session Name" = 12 chars * 10 = 120px width, 12px height
    // Header Box should:
    // - Stretch to column width (180 - 8 padding = 172) on cross-axis
    // - Wrap text height (12px) on main-axis
    CHECK(header->layout.width == doctest::Approx(172));  // Column width minus padding
    CHECK(header->layout.height == doctest::Approx(12));  // Text height
    CHECK(headerText->layout.height == doctest::Approx(12));
}

TEST_CASE("Box wraps Text WITHOUT measure function (fallback)") {
    // No measurer installed -> fallback heuristic (0.6 * fontSize per char).
    MeasureHarness h;

    auto tree = Column(
        Box(Text("Session Name").fontSize(12).setKey("txt"))
            .backgroundColor(0x202020FF)
            .justifyContent(JustifyContent::Center)
            .alignItems(AlignItems::Center)
            .setKey("header"),
        Box().flexGrow(1).setKey("content")
    )
    .width(180)
    .height(480)
    .padding(4)
    .gap(4);

    auto* root = h.mount(std::move(tree));
    root->calculateLayout(180, 480);

    auto* header = root->children[0].get();
    auto* headerText = header->children[0].get();

    MESSAGE("(No measure) Header Box layout: " << header->layout.width << "x" << header->layout.height);
    MESSAGE("(No measure) Header Text layout: " << headerText->layout.width << "x" << headerText->layout.height);

    // With fallback: 12 chars * 12 * 0.6 = 86.4px width, 12px height
    // Box should still wrap the text height correctly
    CHECK(header->layout.height == doctest::Approx(12));
    CHECK(headerText->layout.height == doctest::Approx(12));
}

TEST_CASE("Box with explicit width wraps Text into multiple lines - renderMissingStatus pattern") {
    FnMeasurer measurer(perChar10());
    MeasureHarness h;
    h.setMeasurer(&measurer);

    // This is the exact pattern from renderMissingStatus:
    // Box with explicit width(100) but no height, containing Label
    std::string text = "Missing 7 modules (3 in patch)";  // 300px, box is 100px
    auto box = Box(Text(text).fontSize(12).color(0xFFFFFFFF))
        .backgroundColor(0xFF0000FF)
        .justifyContent(JustifyContent::Center)
        .alignItems(AlignItems::Center)
        .width(100);

    // Put it in a Column like the real app
    auto tree = Column(
        std::move(box),
        Box().flexGrow(1).setKey("content")
    )
    .width(180)
    .height(480)
    .padding(4)
    .gap(4);

    auto* root = h.mount(std::move(tree));
    root->calculateLayout(180, 480);

    auto* statusBox = root->children[0].get();
    auto* statusText = statusBox->children[0].get();

    MESSAGE("Status Box layout: " << statusBox->layout.width << "x" << statusBox->layout.height);
    MESSAGE("Status Text layout: " << statusText->layout.width << "x" << statusText->layout.height);
    MESSAGE("Status Text position: left=" << statusText->layout.left << " top=" << statusText->layout.top);

    // Greedy wrap at 100px (10px/char, spaces dropped at breaks):
    //   "Missing 7"  = 90
    //   "modules (3" = 100
    //   "in patch)"  = 90
    // Text: 100 wide (widest line), 3 lines * 12 = 36 tall; the Box wraps it.
    CHECK(statusBox->layout.width == doctest::Approx(100));
    CHECK(statusBox->layout.height == doctest::Approx(36));
    CHECK(statusText->layout.width == doctest::Approx(100));
    CHECK(statusText->layout.height == doctest::Approx(36));

    // The old single-line measurement centered a 300px line at left=-100,
    // rendering outside the box. Wrapped, the text fits: no overflow.
    CHECK(statusText->layout.left == doctest::Approx(0));
}

TEST_CASE("Header Box without explicit width - stretches and centers text") {
    FnMeasurer measurer(perChar10());
    MeasureHarness h;
    h.setMeasurer(&measurer);

    // This is the header pattern - NO explicit width on Box
    auto tree = Column(
        Box(Text("Session").fontSize(12).setKey("txt"))
            .backgroundColor(0x202020FF)
            .justifyContent(JustifyContent::Center)
            .alignItems(AlignItems::Center)
            .setKey("header"),
        Box().flexGrow(1).setKey("content")
    )
    .width(180)
    .height(480)
    .padding(4)
    .gap(4);

    auto* root = h.mount(std::move(tree));
    root->calculateLayout(180, 480);

    auto* header = root->children[0].get();
    auto* headerText = header->children[0].get();

    MESSAGE("Header Box: " << header->layout.width << "x" << header->layout.height);
    MESSAGE("Header Text: " << headerText->layout.width << "x" << headerText->layout.height);
    MESSAGE("Header Text position: left=" << headerText->layout.left << " top=" << headerText->layout.top);

    // Box should stretch to column inner width (180 - 8 = 172)
    CHECK(header->layout.width == doctest::Approx(172));
    // Box height should wrap text (12px)
    CHECK(header->layout.height == doctest::Approx(12));
    // Text "Session" = 7 chars * 10 = 70px, centered in 172px box
    // left = (172 - 70) / 2 = 51
    CHECK(headerText->layout.left == doctest::Approx(51));
}

TEST_CASE("SessionScreen full structure - missing status box") {
    // No measurer installed: simulates the first frame before a measurer is set.
    // Triggers the fallback heuristic (0.6 * fontSize per char), which wraps
    // with the same shared algorithm a real measurer uses.
    MeasureHarness h;

    // Exact structure from SessionScreen::render()
    auto tree = Column(
        // Header
        Box(Text("Session").fontSize(12).setKey("h_txt"))
            .backgroundColor(0x202020FF)
            .justifyContent(JustifyContent::Center)
            .alignItems(AlignItems::Center)
            .setKey("header"),

        // Missing status - NO explicit width
        Box(Text("Missing 7 modules (3 in patch)").fontSize(12).setKey("m_txt"))
            .backgroundColor(0xFF0000FF)
            .justifyContent(JustifyContent::Center)
            .alignItems(AlignItems::Center)
            .setKey("missing"),

        // User list (scroll)
        Scroll(Box().height(500).setKey("scroll_content"))
            .flexGrow(1)
            .flexShrink(1)
            .setKey("scroll"),

        // Footer
        Row(
            Text("127.0.0.1:9000").fontSize(12).flexGrow(1).setKey("addr"),
            Text("Unlocked").fontSize(12).setKey("lock")
        ).setKey("footer")
    )
    .flexGrow(1)
    .padding(4)
    .gap(4);

    auto* root = h.mount(std::move(tree));
    // Simulate real app dimensions
    root->calculateLayout(180, 480);

    auto* missing = root->children[1].get();
    auto* missingText = missing->children[0].get();

    MESSAGE("Missing Box: " << missing->layout.width << "x" << missing->layout.height);
    MESSAGE("Missing Text: " << missingText->layout.width << "x" << missingText->layout.height);
    MESSAGE("Missing Text pos: left=" << missingText->layout.left << " top=" << missingText->layout.top);

    // Check Yoga node child count
    MESSAGE("Missing Box yoga children: " << YGNodeGetChildCount(missing->yogaNode));
    MESSAGE("Missing Text yoga has measure: " << (YGNodeHasMeasureFunc(missingText->yogaNode) ? "yes" : "no"));

    // Box should stretch to column inner width
    CHECK(missing->layout.width == doctest::Approx(172));
    // Fallback wraps like a real measurer (7.2px/char at fontSize 12):
    //   "Missing 7 modules (3 in" = 165.6 <= 172, "patch)" breaks to line 2.
    // Box height wraps the WRAPPED text height: 2 lines * 12 - THE KEY CHECK.
    CHECK(missing->layout.height == doctest::Approx(24));
    CHECK(missingText->layout.height == doctest::Approx(24));
    CHECK(missingText->layout.width == doctest::Approx(165.6f).epsilon(0.01));
}

// --- Subtree bounds (hit-test prune AABB, synced in syncLayoutFromYoga) ---

TEST_CASE("subtree bounds: leaf equals own rect") {
    Reconciler reconciler;

    auto tree = Box().width(100).height(50);
    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 50);

    CHECK(root->layout.subtreeLeft == doctest::Approx(0));
    CHECK(root->layout.subtreeTop == doctest::Approx(0));
    CHECK(root->layout.subtreeRight == doctest::Approx(100));
    CHECK(root->layout.subtreeBottom == doctest::Approx(50));
}

TEST_CASE("subtree bounds: parent unions an overflowing child, in parent-relative space") {
    Reconciler reconciler;

    auto tree = Box(
                        Box(
                            Box().width(50)
                                 .height(50)
                                 .positionType(PositionType::Absolute)
                                 .positionLeft(80)
                                 .positionTop(80)
                                 .setKey("overflow")
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

    // Child (a leaf): bounds == own rect, relative to ITS parent.
    CHECK(child->layout.subtreeLeft == doctest::Approx(80));
    CHECK(child->layout.subtreeTop == doctest::Approx(80));
    CHECK(child->layout.subtreeRight == doctest::Approx(130));
    CHECK(child->layout.subtreeBottom == doctest::Approx(130));

    // Parent: own 100x100 rect unioned with the child's overhang.
    CHECK(parent->layout.subtreeLeft == doctest::Approx(0));
    CHECK(parent->layout.subtreeTop == doctest::Approx(0));
    CHECK(parent->layout.subtreeRight == doctest::Approx(130));
    CHECK(parent->layout.subtreeBottom == doctest::Approx(130));

    // Root: the parent sits at (0,0), so the overhang propagates unshifted.
    CHECK(root->layout.subtreeRight == doctest::Approx(200));
    CHECK(root->layout.subtreeBottom == doctest::Approx(200));
}

TEST_CASE("subtree bounds: scroll excludes its clipped content") {
    Reconciler reconciler;

    auto tree = Scroll(Box().width(200).height(300)).width(100).height(100);
    auto fiber = reconciler.mount(std::move(tree));
    auto* root = reconciler.renderRoot();
    root->calculateLayout(100, 100);

    // Content is 200x300 but clipped: the scroll's bounds stay its own rect,
    // so no ancestor's hit-test prune widens toward invisible content.
    CHECK(root->layout.subtreeLeft == doctest::Approx(0));
    CHECK(root->layout.subtreeTop == doctest::Approx(0));
    CHECK(root->layout.subtreeRight == doctest::Approx(100));
    CHECK(root->layout.subtreeBottom == doctest::Approx(100));
}
