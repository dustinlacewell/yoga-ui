#include "doctest.h"

#include <yui/core/VNode.hpp>

using namespace yui;

// Helper to get VNode from Child variant
static const VNode& asVNode(const Child& child) {
    return std::get<VNode>(child);
}

TEST_CASE("List generates keyed children from strings") {
    std::vector<std::string> items = {"a", "b", "c"};
    auto list = List(
        items, [](const std::string& s) { return s; }, [](const std::string& s) { return Text(s); });

    CHECK(list.type() == PrimitiveType::Box);
    CHECK(list.children.size() == 3);
    CHECK(asVNode(list.children[0]).key == "a");
    CHECK(asVNode(list.children[1]).key == "b");
    CHECK(asVNode(list.children[2]).key == "c");

    // Check flex direction is column
    CHECK(std::get<BoxProps>(list.props).flexDirection == FlexDirection::Column);
}

TEST_CASE("List generates keyed children from int IDs") {
    struct User {
        int id;
        std::string name;
    };

    std::vector<User> users = {
        {1, "Alice"},
        {2, "Bob"},
        {3, "Carol"},
    };

    auto list = List(
        users, [](const User& u) { return u.id; }, [](const User& u) { return Text(u.name); });

    CHECK(list.children.size() == 3);

    // Int keys use intKey field, not string key
    CHECK(asVNode(list.children[0]).intKey == 1);
    CHECK(asVNode(list.children[1]).intKey == 2);
    CHECK(asVNode(list.children[2]).intKey == 3);

    // Check text content
    CHECK(std::get<TextProps>(asVNode(list.children[0]).props).text == "Alice");
    CHECK(std::get<TextProps>(asVNode(list.children[1]).props).text == "Bob");
    CHECK(std::get<TextProps>(asVNode(list.children[2]).props).text == "Carol");
}

TEST_CASE("HList creates horizontal list") {
    std::vector<std::string> items = {"x", "y", "z"};
    auto list = HList(
        items, [](const std::string& s) { return s; }, [](const std::string& s) { return Box().width(50); });

    CHECK(list.children.size() == 3);
    CHECK(std::get<BoxProps>(list.props).flexDirection == FlexDirection::Row);
}

TEST_CASE("List with empty vector") {
    std::vector<int> items;
    auto list = List(
        items, [](int i) { return i; }, [](int i) { return Text(std::to_string(i)); });

    CHECK(list.children.empty());
}

TEST_CASE("List with complex render function") {
    struct Item {
        std::string id;
        std::string label;
        bool active;
    };

    std::vector<Item> items = {
        {"item-1", "First", true},
        {"item-2", "Second", false},
    };

    auto list = List(
        items, [](const Item& i) { return i.id; },
        [](const Item& i) {
            return Row(
                           Text(i.label).flexGrow(1),
                           When(i.active, Text("*"))
                       )
                .height(24)
                .padding(4);
        });

    CHECK(list.children.size() == 2);
    CHECK(asVNode(list.children[0]).key == "item-1");

    // First item has 2 children (text + active indicator)
    CHECK(asVNode(list.children[0]).children.size() == 2);

    // Second item's "When" produces empty node that wasn't added
    // Actually When returns empty VNode, which gets added but isEmpty=true
    CHECK(asVNode(list.children[1]).children.size() == 2);
    CHECK(asVNode(asVNode(list.children[1]).children[1]).isEmpty);
}
