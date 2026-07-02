#include "doctest.h"

#include <yui/core/VNode.hpp>

#include <type_traits>

using namespace yui;

// The move-props-through-reconcile seam only pays off if VNode/Child/PropsVariant
// have NOEXCEPT moves: a throwing move makes std::vector<VNode>/std::vector<Child>
// reallocation fall back to COPY, silently defeating the whole optimization. These
// static_asserts fail the build if any implicit move ctor becomes throwing.
static_assert(std::is_nothrow_move_constructible_v<VNode>, "VNode move must be noexcept");
static_assert(std::is_nothrow_move_constructible_v<Child>, "Child move must be noexcept");
static_assert(std::is_nothrow_move_constructible_v<PropsVariant>, "PropsVariant move must be noexcept");
static_assert(std::is_nothrow_move_assignable_v<PropsVariant>, "PropsVariant move-assign must be noexcept");
static_assert(std::is_nothrow_move_constructible_v<Component>, "Component move must be noexcept");

TEST_CASE("Box factory creates box node") {
    VNode node = Box();
    CHECK(node.type() == PrimitiveType::Box);
    CHECK(node.children.empty());
    CHECK(std::holds_alternative<BoxProps>(node.props));
}

TEST_CASE("Box with children") {
    VNode node = Box({
        Text("A"),
        Text("B"),
    });
    CHECK(node.children.size() == 2);
    CHECK(std::get<VNode>(node.children[0]).type() == PrimitiveType::Text);
    CHECK(std::get<VNode>(node.children[1]).type() == PrimitiveType::Text);
}

TEST_CASE("Text factory sets text content") {
    VNode node = Text("Hello");
    CHECK(node.type() == PrimitiveType::Text);
    CHECK(std::get<TextProps>(node.props).text == "Hello");
}

TEST_CASE("Input factory creates input node") {
    VNode node = Input().value("test");
    CHECK(node.type() == PrimitiveType::Input);
    CHECK(std::get<InputProps>(node.props).value == "test");
}

TEST_CASE("type() derives from props for each primitive") {
    CHECK(VNode(Box()).type() == PrimitiveType::Box);
    CHECK(VNode(Text("x")).type() == PrimitiveType::Text);
    CHECK(VNode(Input()).type() == PrimitiveType::Input);
    CHECK(VNode(Scroll(Box())).type() == PrimitiveType::Scroll);
    CHECK(VNode(Canvas([](void*, float, float) {})).type() == PrimitiveType::Canvas);
}

TEST_CASE("Fluent layout props") {
    VNode node = Box().width(100).height(50).padding(8).flexGrow(1).flexDirection(FlexDirection::Row);

    auto& p = std::get<BoxProps>(node.props);
    CHECK(p.width == 100);
    CHECK(p.height == 50);
    CHECK(p.padding == 8);
    CHECK(p.flexGrow == 1);
    CHECK(p.flexDirection == FlexDirection::Row);
}

TEST_CASE("Fluent box-specific props") {
    VNode node = Box().backgroundColor(0xFF0000).borderRadius(4).borderWidth(1).borderColor(0x000000);

    auto& p = std::get<BoxProps>(node.props);
    CHECK(p.backgroundColor == 0xFF0000);
    CHECK(p.borderRadius == 4);
    CHECK(p.borderWidth == 1);
    CHECK(p.borderColor == 0x000000);
}

TEST_CASE("Fluent text-specific props") {
    VNode node = Text("Hello").fontSize(14).color(0xFFFFFF);

    auto& p = std::get<TextProps>(node.props);
    CHECK(p.text == "Hello");
    CHECK(p.fontSize == 14);
    CHECK(p.color == 0xFFFFFF);
}

TEST_CASE("Fluent input-specific props") {
    bool changed = false;
    bool submitted = false;

    VNode node = Input()
                     .value("initial")
                     .placeholder("Enter text")
                     .password(true)
                     .onChange([&](const std::string&) { changed = true; })
                     .onSubmit([&]() { submitted = true; });

    auto& p = std::get<InputProps>(node.props);
    CHECK(p.value == "initial");
    CHECK(p.placeholder == "Enter text");
    CHECK(p.password == true);
    CHECK(p.onChange);
    CHECK(p.onSubmit);

    // Verify callbacks work
    p.onChange("");
    p.onSubmit();
    CHECK(changed);
    CHECK(submitted);
}

TEST_CASE("Event props on all primitives") {
    bool clicked = false;
    auto onClick = [&]() { clicked = true; };

    SUBCASE("Box") {
        VNode node = Box().onClick(onClick);
        std::get<BoxProps>(node.props).onClick();
        CHECK(clicked);
    }

    SUBCASE("Text") {
        VNode node = Text("click me").onClick(onClick);
        std::get<TextProps>(node.props).onClick();
        CHECK(clicked);
    }

    SUBCASE("Input") {
        VNode node = Input().onClick(onClick);
        std::get<InputProps>(node.props).onClick();
        CHECK(clicked);
    }
}

TEST_CASE("Key for reconciliation") {
    VNode node = Box().setKey("my-key");
    CHECK(node.key == "my-key");
}

TEST_CASE("Row helper") {
    VNode node = Row({Text("A"), Text("B")});
    CHECK(node.type() == PrimitiveType::Box);
    CHECK(std::get<BoxProps>(node.props).flexDirection == FlexDirection::Row);
    CHECK(node.children.size() == 2);
}

TEST_CASE("Column helper") {
    VNode node = Column({Text("A"), Text("B")});
    CHECK(std::get<BoxProps>(node.props).flexDirection == FlexDirection::Column);
}

TEST_CASE("Spacer helper") {
    VNode node = Spacer();
    CHECK(std::get<BoxProps>(node.props).flexGrow == 1);
}

TEST_CASE("Gap helper") {
    VNode node = Gap(10);
    auto& p = std::get<BoxProps>(node.props);
    CHECK(p.width == 10);
    CHECK(p.height == 10);
}

TEST_CASE("When conditional") {
    SUBCASE("true returns node") {
        auto node = When(true, Text("visible"));
        CHECK(!node.isEmpty);
        CHECK(node.type() == PrimitiveType::Text);
    }

    SUBCASE("false returns empty") {
        auto node = When(false, Text("hidden"));
        CHECK(node.isEmpty);
    }
}

TEST_CASE("If conditional") {
    auto node = If(true, Text("yes"), Text("no"));
    CHECK(std::get<TextProps>(node.props).text == "yes");

    node = If(false, Text("yes"), Text("no"));
    CHECK(std::get<TextProps>(node.props).text == "no");
}

TEST_CASE("Empty node") {
    auto node = VNode::empty();
    CHECK(node.isEmpty);
}
