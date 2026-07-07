#include "doctest.h"

#include <yui/widgets/Tabs.hpp>
#include <yui/yui.hpp>

#include <functional>
#include <string>
#include <vector>

using namespace yui;

namespace {

// First node with this STRING key in pre-order (the test_modal idiom).
Node* findByKey(Node* node, const std::string& key) {
    if (node->key == key)
        return node;
    for (auto& child : node->children)
        if (Node* found = findByKey(child.get(), key))
            return found;
    return nullptr;
}

// First node with this INT key in pre-order. Tab buttons are int-keyed
// (.setKey(i)); the reconciler stores that in Node::intKey, not the string key.
Node* findByIntKey(Node* node, int64_t key) {
    if (node->intKey == key)
        return node;
    for (auto& child : node->children)
        if (Node* found = findByIntKey(child.get(), key))
            return found;
    return nullptr;
}

bool hasKey(Node* node, const std::string& key) {
    return findByKey(node, key) != nullptr;
}

// Center of a node's drawn rect, in root space.
struct Pt {
    float x, y;
};
Pt center(Node* n) {
    auto r = absoluteRect(n);
    return {r.x + r.w / 2, r.y + r.h / 2};
}

}  // namespace

// ---------------------------------------------------------------------------
// Click a tab: onChange fires with that index; clicking the ALREADY-active tab
// fires nothing. `active` is app state (a useState in the host component) so a
// change re-renders (a bare captured int would not mark the host dirty).
// ---------------------------------------------------------------------------

TEST_CASE("Tabs - clicking an inactive tab fires onChange; clicking the active tab does not") {
    Host host;
    int lastChange = -1;
    std::function<void(int)> setActive;
    host.setRender(std::function<VNode()>([&] {
        return Box(Component([&](ComponentContext& ctx) -> VNode {
            auto [active, set] = ctx.useState<int>(0);
            setActive = set;
            return Box(widgets::Tabs()
                           .tab("One", Box().width(50).height(50).setKey("p0"))
                           .tab("Two", Box().width(50).height(50).setKey("p1"))
                           .active(active)
                           .onChange([&, set](int i) {
                               lastChange = i;
                               set(i);
                           }))
                .width(400)
                .height(300);
        }));
    }));
    host.update(400, 300);

    // Tab 0 is active (intKey 0), tab 1 inactive (intKey 1).
    Node* tab1 = findByIntKey(host.root(), 1);
    REQUIRE(tab1 != nullptr);
    auto c1 = center(tab1);
    host.handleMouseDown(c1.x, c1.y, MouseButton::Left);
    host.handleMouseUp(c1.x, c1.y, MouseButton::Left);
    CHECK(lastChange == 1);

    host.update(400, 300);  // re-render with active == 1

    // Click the now-active tab 1 again: no onChange.
    lastChange = -1;
    Node* tab1b = findByIntKey(host.root(), 1);
    REQUIRE(tab1b != nullptr);
    auto c1b = center(tab1b);
    host.handleMouseDown(c1b.x, c1b.y, MouseButton::Left);
    host.handleMouseUp(c1b.x, c1b.y, MouseButton::Left);
    CHECK(lastChange == -1);
}

// ---------------------------------------------------------------------------
// Only the active panel is instantiated; switching swaps which panel content is
// present and re-keys the wrapper "panel-<active>".
// ---------------------------------------------------------------------------

TEST_CASE("Tabs - only the active panel renders; switching swaps content") {
    Host host;
    std::function<void(int)> setActive;
    host.setRender(std::function<VNode()>([&] {
        return Box(Component([&](ComponentContext& ctx) -> VNode {
            auto [active, set] = ctx.useState<int>(0);
            setActive = set;
            return Box(widgets::Tabs()
                           .tab("One", Box().width(50).height(50).setKey("p0"))
                           .tab("Two", Box().width(50).height(50).setKey("p1"))
                           .active(active)
                           .onChange([set](int i) { set(i); }))
                .width(400)
                .height(300);
        }));
    }));
    host.update(400, 300);

    CHECK(hasKey(host.root(), "p0"));
    CHECK(!hasKey(host.root(), "p1"));
    CHECK(hasKey(host.root(), "panel-0"));

    setActive(1);
    host.update(400, 300);

    CHECK(!hasKey(host.root(), "p0"));
    CHECK(hasKey(host.root(), "p1"));
    CHECK(hasKey(host.root(), "panel-1"));
    CHECK(!hasKey(host.root(), "panel-0"));
}

// ---------------------------------------------------------------------------
// REMOUNT-ON-SWITCH: a stateful panel's component state resets when you switch
// away and back — the panel wrapper's per-index key forces an unmount/mount
// rather than reconciling one panel's state into another.
// ---------------------------------------------------------------------------

// Read the text of the first Text node under a stable-keyed marker.
namespace {
std::string textAtKey(Node* node, const std::string& key) {
    Node* holder = findByKey(node, key);
    if (!holder)
        return "<none>";
    // The Text node is the holder itself or its descendant.
    for (Node* n = holder;;) {
        if (n->type() == PrimitiveType::Text)
            return static_cast<TextNode*>(n)->props.text;
        if (n->children.empty())
            return "<no-text>";
        n = n->children.front().get();
    }
}
}  // namespace

TEST_CASE("Tabs - panel component state resets on switch-away-and-back (remount)") {
    Host host;
    std::function<void(int)> setActive;
    std::function<void()> bump;  // increments panel 0's internal counter

    // Panel 0 holds a useState counter and renders it as the TEXT of a
    // stable-keyed node ("counter"). Encoding the count in the node's CONTENT
    // (not its key) keeps the component's own fiber identity stable within a
    // mount, so state accumulates across re-renders. If the panel merely
    // reconciled across tab switches the counter would survive; the panel
    // wrapper's per-index key forces a remount instead, resetting it to 0.
    auto counter = [&](ComponentContext& ctx) -> VNode {
        auto [n, setN] = ctx.useState<int>(0);
        bump = [setN, n] { setN(n + 1); };
        return Box(Text(std::to_string(n))).setKey("counter");
    };

    host.setRender(std::function<VNode()>([&] {
        return Box(Component([&](ComponentContext& ctx) -> VNode {
            auto [active, set] = ctx.useState<int>(0);
            setActive = set;
            return Box(widgets::Tabs()
                           .tab("One", Component(counter))
                           .tab("Two", Box().width(50).height(50).setKey("p1"))
                           .active(active)
                           .onChange([set](int i) { set(i); }))
                .width(400)
                .height(300);
        }));
    }));
    host.update(400, 300);
    CHECK(textAtKey(host.root(), "counter") == "0");

    // Bump the counter twice, re-rendering each time: state accumulates within
    // the mount.
    bump();
    host.update(400, 300);
    bump();
    host.update(400, 300);
    CHECK(textAtKey(host.root(), "counter") == "2");

    // Switch to tab 1 (panel 0 unmounts), then back to tab 0 (panel 0 mounts
    // fresh): the counter is back at its initial 0, proving the remount.
    setActive(1);
    host.update(400, 300);
    CHECK(!hasKey(host.root(), "counter"));  // panel 0 is gone entirely

    setActive(0);
    host.update(400, 300);
    CHECK(textAtKey(host.root(), "counter") == "0");
}

// ---------------------------------------------------------------------------
// Keyboard nav (opt-in): with prev/next keycodes set, focusing the strip (by
// clicking a tab) then pressing next/prev cycles onChange, wrapping at ends.
// ---------------------------------------------------------------------------

TEST_CASE("Tabs - arrow keys cycle the active tab with wraparound when nav codes are set") {
    constexpr int kLeft = 100;
    constexpr int kRight = 200;

    Host host;
    int lastChange = -1;
    std::function<void(int)> setActive;
    host.setRender(std::function<VNode()>([&] {
        return Box(Component([&](ComponentContext& ctx) -> VNode {
            auto [active, set] = ctx.useState<int>(0);
            setActive = set;
            return Box(widgets::Tabs()
                           .tab("One", Box().width(50).height(50).setKey("p0"))
                           .tab("Two", Box().width(50).height(50).setKey("p1"))
                           .tab("Three", Box().width(50).height(50).setKey("p2"))
                           .active(active)
                           .prevKeyCode(kLeft)
                           .nextKeyCode(kRight)
                           .onChange([&, set](int i) {
                               lastChange = i;
                               set(i);
                           }))
                .width(400)
                .height(300);
        }));
    }));
    host.update(400, 300);

    // Focus the strip by clicking the active tab (tab 0): the click walks up to
    // the first focusable ancestor, which is the strip.
    Node* tab0 = findByIntKey(host.root(), 0);
    REQUIRE(tab0 != nullptr);
    auto c0 = center(tab0);
    host.handleMouseDown(c0.x, c0.y, MouseButton::Left);
    host.handleMouseUp(c0.x, c0.y, MouseButton::Left);

    // Next: 0 -> 1.
    host.handleKeyDown(kRight, KeyMod_None);
    CHECK(lastChange == 1);
    host.update(400, 300);

    // Next: 1 -> 2.
    host.handleKeyDown(kRight, KeyMod_None);
    CHECK(lastChange == 2);
    host.update(400, 300);

    // Next at the end wraps: 2 -> 0.
    host.handleKeyDown(kRight, KeyMod_None);
    CHECK(lastChange == 0);
    host.update(400, 300);

    // Prev at the start wraps: 0 -> 2.
    host.handleKeyDown(kLeft, KeyMod_None);
    CHECK(lastChange == 2);
}

// ---------------------------------------------------------------------------
// fill(): the widget root grows to its parent, so the panel gets definite
// height (parent minus strip). Load-bearing for Scroll-hosting panels: scroll
// content is DETACHED from the Yoga tree, so a Scroll has no intrinsic height
// and collapses to zero inside a content-sized Tabs.
// ---------------------------------------------------------------------------
TEST_CASE("Tabs - fill() gives the panel definite height; a Scroll panel gets it all") {
    Host host;
    host.setRender(std::function<VNode()>([] {
        return Box(widgets::Tabs()
                       .tab("A", Scroll(Box().height(500)).flexGrow(1).flexShrink(1))
                       .tab("B", Text("other"))
                       .fill())
            .width(300)
            .height(400);
    }));
    host.update(300, 400);

    Node* panel = findByKey(host.root(), "panel-0");
    REQUIRE(panel != nullptr);
    // Strip is content-sized (~tab padding + font); the panel gets the rest.
    CHECK(panel->layout.height > 300.0f);
    // The Scroll viewport fills the panel instead of collapsing to zero.
    REQUIRE(panel->children.size() == 1);
    CHECK(panel->children[0]->layout.height == doctest::Approx(panel->layout.height));
}

TEST_CASE("Tabs - without fill() the widget stays content-sized among siblings") {
    Host host;
    host.setRender(std::function<VNode()>([] {
        return Box(
                   Text("above").setKey("above"),
                   widgets::Tabs()
                       .tab("A", Text("intrinsic").setKey("intrinsic"))
                       .tab("B", Text("other")),
                   Text("below").setKey("below"))
            .width(300)
            .height(400);
    }));
    host.update(300, 400);

    Node* panel = findByKey(host.root(), "panel-0");
    REQUIRE(panel != nullptr);
    // Panel hugs its intrinsic content; the widget does NOT eat leftover space.
    CHECK(panel->layout.height < 60.0f);
    Node* below = findByKey(host.root(), "below");
    REQUIRE(below != nullptr);
    CHECK(below->layout.top < 100.0f);
}
