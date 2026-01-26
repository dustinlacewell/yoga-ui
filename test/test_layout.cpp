#include "doctest.h"

#include <yui/core/Measure.hpp>
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

TEST_CASE("Box wraps Text child (intrinsic sizing)") {
    Reconciler reconciler;

    // Set up text measure function
    Measure::setTextMeasure([](const std::string& text, float fontSize, float /*maxWidth*/) -> Size {
        return {static_cast<float>(text.length()) * 10.0f, fontSize};
    });

    // Box wrapping Text with no explicit dimensions - should inherit child's size
    // This is the pattern: Box(Label(text)).backgroundColor(...)
    auto tree = Box(Text("Hello").fontSize(16).setKey("txt"))
        .justifyContent(JustifyContent::Center)
        .alignItems(AlignItems::Center)
        .setKey("box");

    auto root = reconciler.mount(tree);
    root->calculateLayout(YGUndefined, YGUndefined);

    // "Hello" = 5 chars * 10px = 50px width, 16px height
    // Box should wrap to fit the text
    CHECK(root->layout.width == doctest::Approx(50));
    CHECK(root->layout.height == doctest::Approx(16));
}

TEST_CASE("Box wraps Text child in Column with FlexStart") {
    Reconciler reconciler;

    Measure::setTextMeasure([](const std::string& text, float fontSize, float /*maxWidth*/) -> Size {
        return {static_cast<float>(text.length()) * 10.0f, fontSize};
    });

    // Box inside Column with alignItems FlexStart (prevents stretch)
    auto tree = Column({
        Box(Text("Hello").fontSize(16).setKey("txt"))
            .justifyContent(JustifyContent::Center)
            .alignItems(AlignItems::Center)
            .setKey("box")
    }).width(200).alignItems(AlignItems::FlexStart);

    auto root = reconciler.mount(tree);
    root->calculateLayout(200, YGUndefined);

    // Box should wrap text, not stretch to column width
    CHECK(root->children[0]->layout.width == doctest::Approx(50));
    CHECK(root->children[0]->layout.height == doctest::Approx(16));
}

TEST_CASE("Box wraps Text in Column - real app scenario") {
    Reconciler reconciler;

    Measure::setTextMeasure([](const std::string& text, float fontSize, float /*maxWidth*/) -> Size {
        return {static_cast<float>(text.length()) * 10.0f, fontSize};
    });

    // Mimics the SessionScreen layout:
    // Column with fixed dimensions, Box header wrapping Text
    auto tree = Column({
        // Header Box - no explicit size, should wrap text HEIGHT
        Box(Text("Session Name").fontSize(12).setKey("txt"))
            .backgroundColor(0x202020FF)
            .justifyContent(JustifyContent::Center)
            .alignItems(AlignItems::Center)
            .setKey("header"),
        // Other content
        Box().flexGrow(1).setKey("content")
    })
    .width(180)
    .height(480)
    .padding(4)
    .gap(4);

    auto root = reconciler.mount(tree);
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
    Reconciler reconciler;

    // Clear measure function - use fallback (0.6 * fontSize per char)
    Measure::setTextMeasure(nullptr);

    auto tree = Column({
        Box(Text("Session Name").fontSize(12).setKey("txt"))
            .backgroundColor(0x202020FF)
            .justifyContent(JustifyContent::Center)
            .alignItems(AlignItems::Center)
            .setKey("header"),
        Box().flexGrow(1).setKey("content")
    })
    .width(180)
    .height(480)
    .padding(4)
    .gap(4);

    auto root = reconciler.mount(tree);
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

TEST_CASE("Box with explicit width wraps Text height - renderMissingStatus pattern") {
    Reconciler reconciler;

    Measure::setTextMeasure([](const std::string& text, float fontSize, float /*maxWidth*/) -> Size {
        return {static_cast<float>(text.length()) * 10.0f, fontSize};
    });

    // This is the exact pattern from renderMissingStatus:
    // Box with explicit width(100) but no height, containing Label
    std::string text = "Missing 7 modules (3 in patch)";  // 30 chars
    auto box = Box(Text(text).fontSize(12).color(0xFFFFFFFF))
        .backgroundColor(0xFF0000FF)
        .justifyContent(JustifyContent::Center)
        .alignItems(AlignItems::Center)
        .width(100);

    // Put it in a Column like the real app
    auto tree = Column({
        std::move(box),
        Box().flexGrow(1).setKey("content")
    })
    .width(180)
    .height(480)
    .padding(4)
    .gap(4);

    auto root = reconciler.mount(tree);
    root->calculateLayout(180, 480);

    auto* statusBox = root->children[0].get();
    auto* statusText = statusBox->children[0].get();

    MESSAGE("Status Box layout: " << statusBox->layout.width << "x" << statusBox->layout.height);
    MESSAGE("Status Text layout: " << statusText->layout.width << "x" << statusText->layout.height);
    MESSAGE("Status Text position: left=" << statusText->layout.left << " top=" << statusText->layout.top);

    // Box width = 100 (explicit)
    // Text width = 30 chars * 10 = 300px (but Box is only 100, text won't fit!)
    // Box height should wrap text height = 12
    CHECK(statusBox->layout.width == doctest::Approx(100));
    CHECK(statusBox->layout.height == doctest::Approx(12));  // Should wrap text

    // BUG DETECTED: Text is positioned at left=-100 because Center alignment
    // positions content as (containerWidth - contentWidth) / 2 = (100 - 300) / 2 = -100
    // This causes text to render OUTSIDE the box boundaries!
    MESSAGE("BUG: Text left=" << statusText->layout.left << " is negative, will render outside box!");
}

TEST_CASE("Header Box without explicit width - stretches and centers text") {
    Reconciler reconciler;

    Measure::setTextMeasure([](const std::string& text, float fontSize, float /*maxWidth*/) -> Size {
        return {static_cast<float>(text.length()) * 10.0f, fontSize};
    });

    // This is the header pattern - NO explicit width on Box
    auto tree = Column({
        Box(Text("Session").fontSize(12).setKey("txt"))
            .backgroundColor(0x202020FF)
            .justifyContent(JustifyContent::Center)
            .alignItems(AlignItems::Center)
            .setKey("header"),
        Box().flexGrow(1).setKey("content")
    })
    .width(180)
    .height(480)
    .padding(4)
    .gap(4);

    auto root = reconciler.mount(tree);
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
    Reconciler reconciler;

    // Use nullptr to simulate first frame before registerMeasureFunc() is called
    // This triggers the fallback: width = text.length() * fontSize * 0.6, height = fontSize
    Measure::setTextMeasure(nullptr);

    // Exact structure from SessionScreen::render()
    auto tree = Column({
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
        Row({
            Text("127.0.0.1:9000").fontSize(12).flexGrow(1).setKey("addr"),
            Text("Unlocked").fontSize(12).setKey("lock")
        }).setKey("footer")
    })
    .flexGrow(1)
    .padding(4)
    .gap(4);

    auto root = reconciler.mount(tree);
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

    // With fallback: 30 chars * 12 * 0.6 = 216px width, 12px height
    // Box should stretch to column inner width
    CHECK(missing->layout.width == doctest::Approx(172));
    // Box height should wrap text height (12px) - THIS IS THE KEY CHECK
    CHECK(missing->layout.height >= 10);  // Should be ~12, NOT 0!
}
