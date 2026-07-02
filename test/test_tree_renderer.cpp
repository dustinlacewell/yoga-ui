#include "TestMeasurer.hpp"
#include "doctest.h"

#include <yui/core/RenderDefaults.hpp>
#include <yui/core/VNode.hpp>
#include <yui/render/TreeRenderer.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

using namespace yui;
using yui::test::MeasureHarness;
namespace rd = yui::render_defaults;

namespace {

// A recording IRenderBackend: every primitive call becomes a Call record, so
// tests assert on the exact draw stream the neutral walk produces — order,
// geometry, colors, clip balance — with no real backend involved.
struct Call {
    enum class Kind { FillRect, StrokeRect, PushClip, PopClip, TextRun, Canvas };
    explicit Call(Kind k) : kind(k) {}
    Kind kind;
    render::Rect rect{};
    uint32_t color = 0;
    float radius = 0;
    float strokeWidth = 0;
    std::string text;
    float x = 0;
    float y = 0;
    float fontSize = 0;
    std::string font;
};

class FakeBackend : public render::IRenderBackend {
public:
    std::vector<Call> calls;

    // 10px per character, fontSize for height — deterministic caret arithmetic.
    Size measure(const std::string& text, float fontSize, float /*maxWidth*/,
                 const std::string& /*font*/) const override {
        return {static_cast<float>(text.length()) * 10.0f, fontSize};
    }

    void beginFrame() override {}
    void endFrame() override {}

    void fillRect(const render::Rect& r, uint32_t color, float radius) override {
        Call c(Call::Kind::FillRect);
        c.rect = r;
        c.color = color;
        c.radius = radius;
        calls.push_back(std::move(c));
    }

    void strokeRect(const render::Rect& r, uint32_t color, float radius, float width) override {
        Call c(Call::Kind::StrokeRect);
        c.rect = r;
        c.color = color;
        c.radius = radius;
        c.strokeWidth = width;
        calls.push_back(std::move(c));
    }

    void pushClip(const render::Rect& r, float radius) override {
        Call c(Call::Kind::PushClip);
        c.rect = r;
        c.radius = radius;
        calls.push_back(std::move(c));
    }

    void popClip() override { calls.push_back(Call(Call::Kind::PopClip)); }

    void drawTextRun(const std::string& run, float x, float y, float fontSize, uint32_t color,
                     const std::string& font) override {
        Call c(Call::Kind::TextRun);
        c.text = run;
        c.x = x;
        c.y = y;
        c.fontSize = fontSize;
        c.color = color;
        c.font = font;
        calls.push_back(std::move(c));
    }

    void drawCanvas(const CanvasNode& node, const render::Rect& r) override {
        Call c(Call::Kind::Canvas);
        c.rect = r;
        calls.push_back(std::move(c));
        // Backend contract: a throwing user callback is isolated here and never
        // unwinds into the walk.
        try {
            node.props.draw(nullptr, r.w, r.h);
        } catch (...) {}
    }

    int count(Call::Kind k) const {
        int n = 0;
        for (const auto& c : calls)
            if (c.kind == k)
                ++n;
        return n;
    }

    const Call* nthOf(Call::Kind k, int n) const {
        for (const auto& c : calls) {
            if (c.kind == k && n-- == 0)
                return &c;
        }
        return nullptr;
    }
};

void checkRect(const render::Rect& r, float x, float y, float w, float h) {
    CHECK(r.x == doctest::Approx(x));
    CHECK(r.y == doctest::Approx(y));
    CHECK(r.w == doctest::Approx(w));
    CHECK(r.h == doctest::Approx(h));
}

// Caret blink is wall-clock this commit (made injectable in a later commit).
// Spin until we are comfortably inside the on-phase — with a 100ms margin, the
// microseconds-long render below cannot cross the blink edge.
void waitForCaretOnPhase() {
    using namespace std::chrono;
    for (;;) {
        auto ms = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        if ((ms % rd::kCaretBlinkPeriodMs) < rd::kCaretBlinkOnMs - 100)
            return;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Draw order: parent chrome before children, children in tree order.
// ---------------------------------------------------------------------------

TEST_CASE("renderTree: draws parent chrome first, then children in order") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Column(Box().width(50).height(20).backgroundColor(0xAABBCC01u).setKey("box"),
                       Text("Hi").fontSize(10).setKey("txt"))
                    .width(100)
                    .height(100)
                    .backgroundColor(0x00000002u);

    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 100);
    render::renderTree(root, backend, {});

    REQUIRE(backend.calls.size() == 3);

    CHECK(backend.calls[0].kind == Call::Kind::FillRect);
    CHECK(backend.calls[0].color == 0x00000002u);
    checkRect(backend.calls[0].rect, 0, 0, 100, 100);

    CHECK(backend.calls[1].kind == Call::Kind::FillRect);
    CHECK(backend.calls[1].color == 0xAABBCC01u);
    checkRect(backend.calls[1].rect, 0, 0, 50, 20);

    CHECK(backend.calls[2].kind == Call::Kind::TextRun);
    CHECK(backend.calls[2].text == "Hi");
    CHECK(backend.calls[2].x == doctest::Approx(0));
    CHECK(backend.calls[2].y == doctest::Approx(20));  // below the 20px box
    CHECK(backend.calls[2].fontSize == doctest::Approx(10));
    CHECK(backend.calls[2].color == rd::kDefaultTextColor);
}

// ---------------------------------------------------------------------------
// Scroll: absolute clip rect, offset arithmetic, push/pop balance.
// ---------------------------------------------------------------------------

TEST_CASE("renderTree: scroll clips at its absolute rect and offsets children") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Column(Scroll(Box().width(50).height(200).backgroundColor(0x112233FFu).setKey("content"))
                           .width(100)
                           .height(80)
                           .setKey("scr"))
                    .width(200)
                    .height(200)
                    .padding(10);

    auto* root = h.mount(std::move(tree));
    root->calculateLayout(200, 200);

    REQUIRE(root->children[0]->type() == PrimitiveType::Scroll);
    auto* scroll = static_cast<ScrollNode*>(root->children[0].get());
    scroll->scrollOffsetY = 30;

    render::renderTree(root, backend, {});

    // Root box has no chrome; the stream is exactly clip / content / unclip.
    REQUIRE(backend.calls.size() == 3);

    CHECK(backend.calls[0].kind == Call::Kind::PushClip);
    checkRect(backend.calls[0].rect, 10, 10, 100, 80);  // absolute (inside padding)

    CHECK(backend.calls[1].kind == Call::Kind::FillRect);
    checkRect(backend.calls[1].rect, 10, -20, 50, 200);  // y = 10 - 30 scroll offset

    CHECK(backend.calls[2].kind == Call::Kind::PopClip);
    CHECK(backend.count(Call::Kind::PushClip) == backend.count(Call::Kind::PopClip));
}

// ---------------------------------------------------------------------------
// A throwing Canvas callback is isolated by the backend: siblings still draw
// and the clip stack stays balanced.
// ---------------------------------------------------------------------------

TEST_CASE("renderTree: throwing canvas callback leaves clip balanced and siblings drawn") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree =
        Scroll(Canvas([](void*, float, float) { throw std::runtime_error("boom"); }).width(10).height(10).setKey("cnv"),
               Box().width(10).height(10).backgroundColor(0x445566FFu).setKey("after"))
            .width(100)
            .height(100);

    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 100);

    int walkErrors = 0;
    render::renderTree(root, backend, [&](std::string_view, const std::exception*) { ++walkErrors; });

    // The walk itself never sees the throw (the backend isolates it).
    CHECK(walkErrors == 0);

    REQUIRE(backend.calls.size() == 4);
    CHECK(backend.calls[0].kind == Call::Kind::PushClip);
    CHECK(backend.calls[1].kind == Call::Kind::Canvas);
    CHECK(backend.calls[2].kind == Call::Kind::FillRect);  // the sibling after the throw
    CHECK(backend.calls[2].color == 0x445566FFu);
    CHECK(backend.calls[3].kind == Call::Kind::PopClip);
    CHECK(backend.count(Call::Kind::PushClip) == backend.count(Call::Kind::PopClip));
}

// ---------------------------------------------------------------------------
// Input chrome and text placement.
// ---------------------------------------------------------------------------

TEST_CASE("renderTree: input draws default chrome and top-left-anchored text") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Input().value("abc").fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);

    render::renderTree(root, backend, {});

    REQUIRE(backend.calls.size() == 3);

    CHECK(backend.calls[0].kind == Call::Kind::FillRect);
    CHECK(backend.calls[0].color == rd::kInputBg);
    CHECK(backend.calls[0].radius == doctest::Approx(rd::kInputBorderRadius));
    checkRect(backend.calls[0].rect, 0, 0, 100, 30);

    CHECK(backend.calls[1].kind == Call::Kind::StrokeRect);
    CHECK(backend.calls[1].color == rd::kInputBorder);
    CHECK(backend.calls[1].strokeWidth == doctest::Approx(rd::kInputBorderWidth));

    CHECK(backend.calls[2].kind == Call::Kind::TextRun);
    CHECK(backend.calls[2].text == "abc");
    CHECK(backend.calls[2].x == doctest::Approx(rd::kInputTextPad));
    CHECK(backend.calls[2].y == doctest::Approx((30 - 10) / 2.0f));  // (h - fontSize) / 2
    CHECK(backend.calls[2].color == rd::kDefaultTextColor);
}

TEST_CASE("renderTree: empty input draws placeholder in placeholder color") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Input().placeholder("hint").fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);

    render::renderTree(root, backend, {});

    const Call* run = backend.nthOf(Call::Kind::TextRun, 0);
    REQUIRE(run != nullptr);
    CHECK(run->text == "hint");
    CHECK(run->color == rd::kPlaceholderColor);
}

TEST_CASE("renderTree: password input masks the display run") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Input().value("abc").password(true).fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);

    render::renderTree(root, backend, {});

    const Call* run = backend.nthOf(Call::Kind::TextRun, 0);
    REQUIRE(run != nullptr);
    CHECK(run->text == "***");
}

// ---------------------------------------------------------------------------
// Caret: a fillRect past the measured run when focused and in the blink
// on-phase; never present when unfocused.
// ---------------------------------------------------------------------------

TEST_CASE("renderTree: focused input in blink on-phase draws the caret fillRect") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Input().value("abc").fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);
    root->focused = true;

    waitForCaretOnPhase();
    render::renderTree(root, backend, {});

    // bg fill + caret fill: the caret is the second FillRect, drawn last.
    REQUIRE(backend.count(Call::Kind::FillRect) == 2);
    const Call* caret = backend.nthOf(Call::Kind::FillRect, 1);
    REQUIRE(caret != nullptr);
    CHECK(caret == &backend.calls.back());

    // x: pad + measured "abc" (3 chars * 10px), centered on the caret width.
    float caretX = rd::kInputTextPad + 30.0f - rd::kCaretWidth / 2;
    checkRect(caret->rect, caretX, rd::kCaretInset, rd::kCaretWidth, 30 - 2 * rd::kCaretInset);
    CHECK(caret->color == rd::kDefaultTextColor);
    CHECK(caret->radius == doctest::Approx(0));
}

TEST_CASE("renderTree: unfocused input never draws a caret") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Input().value("abc").fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);

    render::renderTree(root, backend, {});

    // Only the background fill — no caret regardless of blink phase.
    CHECK(backend.count(Call::Kind::FillRect) == 1);
}

// ---------------------------------------------------------------------------
// Placeholder never advances the caret (displayText is empty).
// ---------------------------------------------------------------------------

TEST_CASE("renderTree: caret sits at the text pad when only a placeholder shows") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Input().placeholder("hint").fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);
    root->focused = true;

    waitForCaretOnPhase();
    render::renderTree(root, backend, {});

    REQUIRE(backend.count(Call::Kind::FillRect) == 2);
    const Call* caret = backend.nthOf(Call::Kind::FillRect, 1);
    REQUIRE(caret != nullptr);
    CHECK(caret->rect.x == doctest::Approx(rd::kInputTextPad - rd::kCaretWidth / 2));  // no advance past "hint"
}
