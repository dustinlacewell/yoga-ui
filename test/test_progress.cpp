#include "doctest.h"
#include "TestMeasurer.hpp"

#include <yui/core/NodeRef.hpp>
#include <yui/widgets/Progress.hpp>
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

TEST_CASE("Progress: fill width tracks value against the track") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    // Wrap in a fixed 200px-wide box so widthPercent(100) resolves.
    host.setRender(std::function<VNode()>([] { return Box(widgets::Progress(0.5f)).width(200).height(40); }));
    host.update(400, 400);

    Node* track = findByKey(host.root(), "track");
    Node* fill = findByKey(host.root(), "fill");
    REQUIRE(track != nullptr);
    REQUIRE(fill != nullptr);

    auto tr = absoluteRect(track);
    auto fr = absoluteRect(fill);
    CHECK(tr.w == doctest::Approx(200.0f));
    CHECK(fr.w == doctest::Approx(100.0f));  // 0.5 * 200
}

TEST_CASE("Progress: value clamps to [0,1]") {
    {
        Host host;
        test::FnMeasurer m = tenPxPerByte();
        host.setTextMeasurer(&m);
        host.setRender(std::function<VNode()>([] { return Box(widgets::Progress(1.5f)).width(200).height(40); }));
        host.update(400, 400);
        auto fr = absoluteRect(findByKey(host.root(), "fill"));
        CHECK(fr.w == doctest::Approx(200.0f));  // clamped to 1.0 -> full
    }
    {
        Host host;
        test::FnMeasurer m = tenPxPerByte();
        host.setTextMeasurer(&m);
        host.setRender(std::function<VNode()>([] { return Box(widgets::Progress(-1.0f)).width(200).height(40); }));
        host.update(400, 400);
        auto fr = absoluteRect(findByKey(host.root(), "fill"));
        CHECK(fr.w == doctest::Approx(0.0f));  // clamped to 0.0 -> empty
    }
}

TEST_CASE("Progress: no handlers — a click over it falls through without throwing") {
    Host host;
    test::FnMeasurer m = tenPxPerByte();
    host.setTextMeasurer(&m);
    host.setRender(std::function<VNode()>([] { return Box(widgets::Progress(0.5f)).width(200).height(40); }));
    host.update(400, 400);
    // The bar has no onClick; a mousedown over it must not be consumed / throw.
    CHECK_FALSE(clickAt(host, 20, 20));
}
