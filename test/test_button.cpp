#include "doctest.h"

#include <yui/widgets/Button.hpp>
#include <yui/yui.hpp>

#include <functional>
#include <string>
#include <vector>

using namespace yui;

namespace {

// First node with this key in pre-order (the test_modal idiom).
Node* findByKey(Node* node, const std::string& key) {
    if (node->key == key)
        return node;
    for (auto& child : node->children)
        if (Node* found = findByKey(child.get(), key))
            return found;
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// Click: a press+release inside the button fires onClick exactly once.
// ---------------------------------------------------------------------------

TEST_CASE("Button - click inside fires onClick once") {
    Host host;
    int clicks = 0;
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::Button(std::string("Go")).onClick([&] { clicks++; }))
            .width(400)
            .height(200)
            .padding(50);
    }));
    host.update(400, 200);

    Node* btn = findByKey(host.root(), "button");
    REQUIRE(btn != nullptr);
    auto r = absoluteRect(btn);
    const float cx = r.x + r.w / 2;
    const float cy = r.y + r.h / 2;

    host.handleMouseDown(cx, cy, MouseButton::Left);
    host.handleMouseUp(cx, cy, MouseButton::Left);
    CHECK(clicks == 1);
}

// ---------------------------------------------------------------------------
// Disabled: no onClick, AND the click is consumed so it never bubbles to a
// parent handler (the load-bearing consuming no-op in the disabled path).
// ---------------------------------------------------------------------------

TEST_CASE("Button - disabled swallows the click; no onClick and no bubble to parent") {
    Host host;
    int clicks = 0;
    int parentClicks = 0;
    host.setRender(std::function<VNode()>([&] {
        // Parent Box wraps the button and has its own onClick counter.
        return Box(widgets::Button(std::string("Nope")).disabled().onClick([&] { clicks++; }))
            .width(400)
            .height(200)
            .padding(50)
            .onClick([&] { parentClicks++; })
            .setKey("parent");
    }));
    host.update(400, 200);

    Node* btn = findByKey(host.root(), "button");
    REQUIRE(btn != nullptr);
    auto r = absoluteRect(btn);
    const float cx = r.x + r.w / 2;
    const float cy = r.y + r.h / 2;

    host.handleMouseDown(cx, cy, MouseButton::Left);
    host.handleMouseUp(cx, cy, MouseButton::Left);
    CHECK(clicks == 0);
    // The button's consuming onClick ran to completion at the button; the click
    // never reaches the parent.
    CHECK(parentClicks == 0);
}

// ---------------------------------------------------------------------------
// Press-inside, release-outside: the click gate requires down+up on the same
// node (test_events "press on A, release on B fires neither"), so releasing off
// the button fires nothing.
// ---------------------------------------------------------------------------

TEST_CASE("Button - press inside then release outside does not fire onClick") {
    Host host;
    int clicks = 0;
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::Button(std::string("Go")).onClick([&] { clicks++; }))
            .width(400)
            .height(200)
            .padding(50);
    }));
    host.update(400, 200);

    Node* btn = findByKey(host.root(), "button");
    REQUIRE(btn != nullptr);
    auto r = absoluteRect(btn);
    const float cx = r.x + r.w / 2;
    const float cy = r.y + r.h / 2;

    host.handleMouseDown(cx, cy, MouseButton::Left);
    host.handleMouseUp(5, 5, MouseButton::Left);  // far outside the button
    CHECK(clicks == 0);
}

// ---------------------------------------------------------------------------
// Cursor: hovering an enabled button shows Pointer; a disabled button shows
// Arrow. (getCursor is the pull query the platform polls each frame.)
// ---------------------------------------------------------------------------

TEST_CASE("Button - enabled hover shows Pointer, disabled shows Arrow") {
    SUBCASE("enabled") {
        Host host;
        host.setRender(std::function<VNode()>(
            [] { return Box(widgets::Button(std::string("Go"))).width(400).height(200).padding(50); }));
        host.update(400, 200);

        Node* btn = findByKey(host.root(), "button");
        REQUIRE(btn != nullptr);
        auto r = absoluteRect(btn);
        host.handleMouseMove(r.x + r.w / 2, r.y + r.h / 2);
        CHECK(host.getCursor() == CursorShape::Pointer);
    }

    SUBCASE("disabled") {
        Host host;
        host.setRender(std::function<VNode()>(
            [] { return Box(widgets::Button(std::string("Go")).disabled()).width(400).height(200).padding(50); }));
        host.update(400, 200);

        Node* btn = findByKey(host.root(), "button");
        REQUIRE(btn != nullptr);
        auto r = absoluteRect(btn);
        host.handleMouseMove(r.x + r.w / 2, r.y + r.h / 2);
        CHECK(host.getCursor() == CursorShape::Arrow);
    }
}

// ---------------------------------------------------------------------------
// Content path: a button built from arbitrary children (not a label) still
// clicks and keys its box "button".
// ---------------------------------------------------------------------------

TEST_CASE("Button - content path (arbitrary children) clicks") {
    Host host;
    int clicks = 0;
    host.setRender(std::function<VNode()>([&] {
        return Box(widgets::Button(Box().width(30).height(30).setKey("glyph")).onClick([&] { clicks++; }))
            .width(400)
            .height(200)
            .padding(50);
    }));
    host.update(400, 200);

    REQUIRE(findByKey(host.root(), "glyph") != nullptr);
    Node* btn = findByKey(host.root(), "button");
    REQUIRE(btn != nullptr);
    auto r = absoluteRect(btn);
    const float cx = r.x + r.w / 2;
    const float cy = r.y + r.h / 2;
    host.handleMouseDown(cx, cy, MouseButton::Left);
    host.handleMouseUp(cx, cy, MouseButton::Left);
    CHECK(clicks == 1);
}
