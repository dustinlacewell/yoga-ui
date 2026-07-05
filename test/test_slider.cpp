#include "doctest.h"

#include <yui/widgets/Slider.hpp>  // direct include: umbrella (yui.hpp) not yet wired
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

// Fake keycodes: core is keycode-agnostic, so the exact ints are arbitrary.
constexpr int kLeftKey = 1001;
constexpr int kRightKey = 1002;

// The wrapper is a fixed-width Box so getBoundingRect has a known width. The
// slider is placed at the window origin, so track.x == 0 and track.w == 200.
constexpr float kWrapperWidth = 200.0f;

// Expected value the impl computes for a pointer at absolute x, given the
// default thumb size (14) over a track of width w anchored at x0 == 0.
float expectedValue(float x, float w = kWrapperWidth, float thumb = widgets::kSliderThumbSize) {
    return widgets::slider_detail::valueFromX(x, layout::Rect{0, 0, w, 0}, thumb);
}

}  // namespace

// ---------------------------------------------------------------------------
// Press-jump: a mousedown on the track snaps the value to the pointer, using
// the thumb-center offset so a press maps exactly as valueFromX computes it.
// ---------------------------------------------------------------------------

TEST_CASE("Slider press-jump maps pointer x to value through the thumb-center offset") {
    Host host;
    float changed = -1.0f;

    auto App = [&](ComponentContext&) -> VNode {
        return Box(widgets::Slider(0.0f).onChange([&](float v) { changed = v; })).width(kWrapperWidth).height(40);
    };
    host.setRender(Component(App));
    host.update(400, 400);

    // Press at 75% across the track. Expected value is what the impl computes.
    const float pressX = 0.75f * kWrapperWidth;
    host.handleMouseDown(pressX, 7, MouseButton::Left);
    CHECK(changed == doctest::Approx(expectedValue(pressX)));
    CHECK(changed == doctest::Approx(0.75f).epsilon(0.05f));  // near 0.75 modulo the offset
    host.handleMouseUp(pressX, 7, MouseButton::Left);
}

// ---------------------------------------------------------------------------
// The load-bearing one: an implicit-capture drag. A press on the wrapper makes
// it the captor, so onMouseMove routes to it even when the pointer travels off
// the node — a move past the right edge clamps to 1.0. After release, capture
// ends and a subsequent move fires nothing.
// ---------------------------------------------------------------------------

TEST_CASE("Slider captured drag: move beyond the right edge clamps to 1.0; release ends capture") {
    Host host;
    int changes = 0;
    float changed = -1.0f;

    auto App = [&](ComponentContext&) -> VNode {
        return Box(widgets::Slider(0.5f).onChange([&](float v) {
                   changed = v;
                   changes++;
               }))
            .width(kWrapperWidth)
            .height(40);
    };
    host.setRender(Component(App));
    host.update(400, 400);

    // Press at the middle -> arms capture.
    host.handleMouseDown(kWrapperWidth / 2.0f, 7, MouseButton::Left);
    const int afterPress = changes;

    // Move far past the right edge of the wrapper: capture routes it to the
    // slider, valueFromX clamps to 1.0.
    host.handleMouseMove(kWrapperWidth + 500.0f, 7);
    CHECK(changed == doctest::Approx(1.0f));
    CHECK(changes > afterPress);

    // Release ends capture.
    host.handleMouseUp(kWrapperWidth + 500.0f, 7, MouseButton::Left);
    const int afterRelease = changes;

    // A move after release fires nothing (dragging flag cleared).
    host.handleMouseMove(20.0f, 7);
    CHECK(changes == afterRelease);
}

// ---------------------------------------------------------------------------
// A move with no preceding press must not fire onChange (the dragging gate —
// onMouseMove also fires on plain hover).
// ---------------------------------------------------------------------------

TEST_CASE("Slider move without a press does not change value") {
    Host host;
    int changes = 0;

    auto App = [&](ComponentContext&) -> VNode {
        return Box(widgets::Slider(0.3f).onChange([&](float) { changes++; })).width(kWrapperWidth).height(40);
    };
    host.setRender(Component(App));
    host.update(400, 400);

    host.handleMouseMove(100.0f, 7);
    host.handleMouseMove(150.0f, 7);
    CHECK(changes == 0);
}

// ---------------------------------------------------------------------------
// Step quantization: a press landing near 0.63 snaps to the nearest 0.1 (0.6).
// ---------------------------------------------------------------------------

TEST_CASE("Slider step quantizes the press value to the nearest step") {
    Host host;
    float changed = -1.0f;

    auto App = [&](ComponentContext&) -> VNode {
        return Box(widgets::Slider(0.0f).step(0.1f).onChange([&](float v) { changed = v; }))
            .width(kWrapperWidth)
            .height(40);
    };
    host.setRender(Component(App));
    host.update(400, 400);

    // Pick an x whose continuous value is ~0.63 (rounds to 0.6). Solve for x:
    // v = (x - thumb/2) / (w - thumb)  =>  x = v*(w-thumb) + thumb/2.
    const float thumb = widgets::kSliderThumbSize;
    const float targetX = 0.63f * (kWrapperWidth - thumb) + thumb / 2.0f;
    // Sanity: continuous value really is ~0.63.
    REQUIRE(expectedValue(targetX) == doctest::Approx(0.63f).epsilon(0.02f));

    host.handleMouseDown(targetX, 7, MouseButton::Left);
    CHECK(changed == doctest::Approx(0.6f));
    host.handleMouseUp(targetX, 7, MouseButton::Left);
}

// ---------------------------------------------------------------------------
// Disabled: every mouse event is ignored, no onChange fires.
// ---------------------------------------------------------------------------

TEST_CASE("Slider disabled ignores all mouse events") {
    Host host;
    int changes = 0;

    auto App = [&](ComponentContext&) -> VNode {
        return Box(widgets::Slider(0.4f).disabled().onChange([&](float) { changes++; }))
            .width(kWrapperWidth)
            .height(40);
    };
    host.setRender(Component(App));
    host.update(400, 400);

    host.handleMouseDown(100.0f, 7, MouseButton::Left);
    host.handleMouseMove(180.0f, 7);
    host.handleMouseUp(180.0f, 7, MouseButton::Left);
    CHECK(changes == 0);
}

// ---------------------------------------------------------------------------
// Keyboard (opt-in): focus by click, then the increment key nudges by keyStep
// (clamped). The default keyStep is 0.05.
// ---------------------------------------------------------------------------

TEST_CASE("Slider keyboard increment nudges by keyStep when focused") {
    Host host;
    float changed = -1.0f;
    float current = 0.5f;

    auto App = [&](ComponentContext&) -> VNode {
        return Box(widgets::Slider(current)
                       .decrementKeyCode(kLeftKey)
                       .incrementKeyCode(kRightKey)
                       .onChange([&](float v) { changed = v; }))
            .width(kWrapperWidth)
            .height(40);
    };
    host.setRender(Component(App));
    host.update(400, 400);

    // Focus the slider by pressing on it (also arms drag, but a press-jump does
    // not affect the keyboard path). Release to end capture.
    host.handleMouseDown(kWrapperWidth / 2.0f, 7, MouseButton::Left);
    host.handleMouseUp(kWrapperWidth / 2.0f, 7, MouseButton::Left);

    host.handleKeyDown(kRightKey, 0, false);
    CHECK(changed == doctest::Approx(0.5f + widgets::kSliderKeyStep));

    host.handleKeyDown(kLeftKey, 0, false);
    CHECK(changed == doctest::Approx(0.5f - widgets::kSliderKeyStep));
}

// ---------------------------------------------------------------------------
// Thumb geometry: after driving the value, the "thumb" node's absolute rect
// center sits at the expected fraction across the track.
// ---------------------------------------------------------------------------

TEST_CASE("Slider thumb center tracks the driven value") {
    Host host;
    float latest = 0.0f;

    // Value in app STATE so onChange -> setter marks the fiber dirty and the next
    // update() actually re-renders the slider at the new value (a bare local, not
    // reaching Host state, would leave update() reusing the cached tree).
    auto App = [&](ComponentContext& ctx) -> VNode {
        auto [value, setValue] = ctx.useState<float>(0.0f);
        return Box(widgets::Slider(value).onChange([&latest, setValue](float v) {
                   latest = v;
                   setValue(v);
               }))
            .width(kWrapperWidth)
            .height(40);
    };
    host.setRender(Component(App));
    host.update(400, 400);

    // Drive to 0.25 via a press, then re-render with the new value.
    const float thumb = widgets::kSliderThumbSize;
    const float pressX = 0.25f * (kWrapperWidth - thumb) + thumb / 2.0f;
    host.handleMouseDown(pressX, 7, MouseButton::Left);
    host.handleMouseUp(pressX, 7, MouseButton::Left);
    host.update(400, 400);

    const float value = latest;
    Node* thumbNode = findByKey(host.root(), "thumb");
    REQUIRE(thumbNode != nullptr);
    layout::Rect tr = absoluteRect(thumbNode);
    const float center = tr.x + tr.w / 2.0f;
    // Expected thumb-center x: fill spans value*(w-thumb), then half a thumb.
    const float expectedCenter = value * (kWrapperWidth - thumb) + thumb / 2.0f;
    CHECK(center == doctest::Approx(expectedCenter).epsilon(0.02f));
}
