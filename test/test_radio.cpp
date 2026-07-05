#include "doctest.h"
#include "TestMeasurer.hpp"

#include <yui/core/NodeRef.hpp>
#include <yui/widgets/Radio.hpp>
#include <yui/yui.hpp>

#include <functional>
#include <string>
#include <vector>

using namespace yui;

namespace {

test::FnMeasurer tenPxPerByte() {
    return test::FnMeasurer(
        [](const std::string& t, float, float) { return Size{static_cast<float>(t.size()) * 10.0f, 10.0f}; });
}

Node* findByKey(Node* node, const std::string& key) {
    if (node->key == key)
        return node;
    for (auto& child : node->children)
        if (Node* found = findByKey(child.get(), key))
            return found;
    return nullptr;
}

bool clickAt(Host& host, float x, float y) {
    host.handleMouseDown(x, y, MouseButton::Left);
    return host.handleMouseUp(x, y, MouseButton::Left);
}

}  // namespace

TEST_CASE("Radio: click fires onSelect") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    bool called = false;
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::Radio(false).onSelect([&] { called = true; })).width(200).height(60);
    }));
    host.update(400, 400);

    clickAt(host, 8, 8);
    CHECK(called);
}

TEST_CASE("RadioGroup: exactly the selected index shows a dot") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::RadioGroup({"a", "b", "c"}).value(1)).width(200).height(120);
    }));
    host.update(400, 400);

    // Each option is keyed by its index; only index 1 carries a "dot" child.
    Node* opt0 = findByKey(host.root(), "0");
    Node* opt1 = findByKey(host.root(), "1");
    Node* opt2 = findByKey(host.root(), "2");
    REQUIRE(opt0 != nullptr);
    REQUIRE(opt1 != nullptr);
    REQUIRE(opt2 != nullptr);
    CHECK(findByKey(opt0, "dot") == nullptr);
    CHECK(findByKey(opt1, "dot") != nullptr);
    CHECK(findByKey(opt2, "dot") == nullptr);
}

TEST_CASE("RadioGroup: clicking an unselected option reports its index") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    int changed = -1;
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::RadioGroup({"a", "b", "c"}).value(0).onChange([&](int i) { changed = i; }))
            .width(200)
            .height(120);
    }));
    host.update(400, 400);

    // Options stack in a Column (default) with gap 6; each Radio circle is 16
    // tall. Option 1's circle sits at y = 16 + 6 = 22 -> center ~30.
    Node* opt1 = findByKey(host.root(), "1");
    REQUIRE(opt1 != nullptr);
    auto r = absoluteRect(opt1);
    clickAt(host, r.x + 8, r.y + 8);
    CHECK(changed == 1);
}

TEST_CASE("RadioGroup: clicking the already-selected option is a no-op") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    int changed = -99;
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::RadioGroup({"a", "b", "c"}).value(1).onChange([&](int i) { changed = i; }))
            .width(200)
            .height(120);
    }));
    host.update(400, 400);

    Node* opt1 = findByKey(host.root(), "1");
    REQUIRE(opt1 != nullptr);
    auto r = absoluteRect(opt1);
    clickAt(host, r.x + 8, r.y + 8);
    CHECK(changed == -99);  // onChange never fired for the current selection
}
