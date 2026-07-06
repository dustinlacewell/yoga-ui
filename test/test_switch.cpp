#include "doctest.h"
#include "TestMeasurer.hpp"

#include <yui/core/NodeRef.hpp>
#include <yui/widgets/Switch.hpp>
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

TEST_CASE("Switch: click fires onChange(!on)") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    bool called = false;
    bool got = true;
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::Switch(false).onChange([&](bool v) {
                       called = true;
                       got = v;
                   }))
            .width(200)
            .height(60);
    }));
    host.update(400, 400);

    // Track is 36x20 at origin; click its center.
    clickAt(host, 18, 10);
    CHECK(called);
    CHECK(got == true);  // !on, on was false
}

TEST_CASE("Switch: disabled click does not fire onChange") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    bool called = false;
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::Switch(false).disabled(true).onChange([&](bool) { called = true; }))
            .width(200)
            .height(60);
    }));
    host.update(400, 400);

    clickAt(host, 18, 10);
    CHECK_FALSE(called);
}

TEST_CASE("Switch: disabled click is consumed (does not bubble to a parent)") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    int parentClicks = 0;
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::Switch(false).disabled(true).onChange([](bool) {}))
            .width(200)
            .height(60)
            .onClick([&] { parentClicks++; });
    }));
    host.update(400, 400);

    clickAt(host, 18, 10);
    CHECK(parentClicks == 0);  // consuming even when disabled
}

TEST_CASE("Switch is CONTROLLED: the chip position follows the passed value, not the click") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    // The app IGNORES onChange, so the passed on=false must stay reflected: the
    // chip stays at the left (off) position after a click.
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::Switch(false).onChange([](bool) {})).width(200).height(60);
    }));
    host.update(400, 400);

    Node* chipOff = findByKey(host.root(), "chip");
    REQUIRE(chipOff != nullptr);
    layout::Rect off = absoluteRect(chipOff);

    clickAt(host, 18, 10);
    host.update(400, 400);

    Node* chipStill = findByKey(host.root(), "chip");
    REQUIRE(chipStill != nullptr);
    layout::Rect still = absoluteRect(chipStill);
    CHECK(still.x == doctest::Approx(off.x));  // unchanged — app never flipped the value
}

TEST_CASE("Switch: the chip slides right when on") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);

    auto chipX = [&](bool on) {
        host.setRender(std::function<VNode()>([&, on] {
            return Box(widgets::Switch(on).onChange([](bool) {})).width(200).height(60);
        }));
        host.update(400, 400);
        Node* chip = findByKey(host.root(), "chip");
        REQUIRE(chip != nullptr);
        return absoluteRect(chip).x;
    };

    CHECK(chipX(true) > chipX(false));  // on-chip is to the right of off-chip
}

TEST_CASE("Switch: toggleKeyCode toggles while focused") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    bool called = false;
    bool got = true;
    constexpr int kSpace = 32;
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::Switch(false).toggleKeyCode(kSpace).onChange([&](bool v) {
                       called = true;
                       got = v;
                   }))
            .width(200)
            .height(60);
    }));
    host.update(400, 400);

    // Focus the switch (click it), then press Space.
    clickAt(host, 18, 10);
    host.handleKeyDown(kSpace, KeyMod_None, false);
    CHECK(called);
    CHECK(got == true);
}

TEST_CASE("Switch: no key handler attached without a toggleKeyCode (keys bubble to app)") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    int appKeys = 0;
    constexpr int kSpace = 32;
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::Switch(false).onChange([](bool) {}))
            .width(200)
            .height(60)
            .onKeyDown([&](int, uint16_t, bool) { appKeys++; });
    }));
    host.update(400, 400);

    // Focus the switch, then send a key — with no toggleKeyCode set, the switch
    // attaches no onKeyDown, so the key bubbles to the app ancestor.
    clickAt(host, 18, 10);
    host.handleKeyDown(kSpace, KeyMod_None, false);
    CHECK(appKeys == 1);  // not black-holed
}
