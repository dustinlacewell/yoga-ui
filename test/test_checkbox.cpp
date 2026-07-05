#include "doctest.h"
#include "TestMeasurer.hpp"

#include <yui/core/NodeRef.hpp>
#include <yui/widgets/Checkbox.hpp>
#include <yui/yui.hpp>

#include <functional>
#include <string>

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

TEST_CASE("Checkbox: click fires onChange(!checked)") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    bool got = false;
    bool called = false;
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::Checkbox(false).onChange([&](bool v) {
                       called = true;
                       got = v;
                   }))
            .width(200)
            .height(60);
    }));
    host.update(400, 400);

    // The box glyph is 16x16 at origin; click its center.
    clickAt(host, 8, 8);
    CHECK(called);
    CHECK(got == true);  // !checked, checked was false
}

TEST_CASE("Checkbox: disabled click does not fire onChange") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    bool called = false;
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::Checkbox(false).disabled(true).onChange([&](bool) { called = true; }))
            .width(200)
            .height(60);
    }));
    host.update(400, 400);

    clickAt(host, 8, 8);
    CHECK_FALSE(called);
}

TEST_CASE("Checkbox is CONTROLLED: the check glyph follows the passed value, not the click") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    // The app deliberately IGNORES onChange (does not flip its state), so the
    // passed `checked=false` must remain reflected in the tree after a click.
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::Checkbox(false).onChange([](bool) {})).width(200).height(60);
    }));
    host.update(400, 400);

    // Before: unchecked -> no inner check glyph.
    CHECK(findByKey(host.root(), "check") == nullptr);

    clickAt(host, 8, 8);
    host.update(400, 400);

    // After a click with app state unchanged: STILL no check glyph. The widget
    // holds no transient checked state — presence tracks the passed value only.
    CHECK(findByKey(host.root(), "check") == nullptr);
}

TEST_CASE("Checkbox: checked=true renders the check glyph") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::Checkbox(true).onChange([](bool) {})).width(200).height(60);
    }));
    host.update(400, 400);
    CHECK(findByKey(host.root(), "check") != nullptr);
}

TEST_CASE("Checkbox: toggleKeyCode toggles while focused") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    constexpr int kSpace = 32;
    bool called = false;
    bool got = false;
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::Checkbox(false).toggleKeyCode(kSpace).onChange([&](bool v) {
                       called = true;
                       got = v;
                   }))
            .width(200)
            .height(60);
    }));
    host.update(400, 400);

    // Focus the checkbox by clicking it (focus lands on the focusable Row).
    clickAt(host, 8, 8);
    REQUIRE(host.getFocusedNode() != nullptr);

    called = false;  // clear the click's onChange
    CHECK(host.handleKeyDown(kSpace, KeyMod_None, false));
    CHECK(called);
    CHECK(got == true);  // !checked
}
