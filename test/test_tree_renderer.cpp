#include "TestMeasurer.hpp"
#include "doctest.h"

#include <yui/core/RenderDefaults.hpp>
#include <yui/core/VNode.hpp>
#include <yui/render/TreeRenderer.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
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

    // 10px per byte, lineHeight == fontSize — deterministic wrap and caret
    // arithmetic (sizing goes through the shared measure() over these).
    float measureRun(std::string_view run, float /*fontSize*/, std::string_view /*font*/) const override {
        return static_cast<float>(run.size()) * 10.0f;
    }

    FontMetrics fontMetrics(float fontSize, std::string_view /*font*/) const override {
        return {0.8f * fontSize, 0.2f * fontSize, fontSize};
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
                     std::string_view font) override {
        Call c(Call::Kind::TextRun);
        c.text = run;
        c.x = x;
        c.y = y;
        c.fontSize = fontSize;
        c.color = color;
        c.font = std::string(font);
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
// Wrapped text: one drawTextRun per line, stepped by lineHeight, with the
// exact substrings the shared wrap produced. wrap(false) opts out.
// ---------------------------------------------------------------------------

TEST_CASE("renderTree: wrapped text draws one run per line, stepped by lineHeight") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    // 10px/char: "aaa bbb" (70px) in a 40px column wraps to "aaa" / "bbb".
    auto tree = Column(Text("aaa bbb").fontSize(10).setKey("t")).width(40).alignItems(AlignItems::FlexStart);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(40, 100);

    // Layout agrees with the wrap: 2 lines of lineHeight (== fontSize) each.
    CHECK(root->children[0]->layout.height == doctest::Approx(20));

    render::renderTree(root, backend, {});

    REQUIRE(backend.count(Call::Kind::TextRun) == 2);
    const Call* r0 = backend.nthOf(Call::Kind::TextRun, 0);
    const Call* r1 = backend.nthOf(Call::Kind::TextRun, 1);
    CHECK(r0->text == "aaa");
    CHECK(r0->x == doctest::Approx(0));
    CHECK(r0->y == doctest::Approx(0));
    CHECK(r1->text == "bbb");
    CHECK(r1->x == doctest::Approx(0));
    CHECK(r1->y == doctest::Approx(10));  // one lineHeight below the first run
}

TEST_CASE("renderTree: padded text wraps at the content width and draws at the content origin") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    // 10px/char, padding 20: the text stretches to the column's 100px, so Yoga
    // measures at the CONTENT width 100 - 2*20 = 60 → "aaaa" / "bbbb", and the
    // node reserves 2 lines + vertical padding. Paint must wrap at that same 60
    // (NOT the 100px border box, where "aaaa bbbb" = 90px fits one line) and
    // start at the content origin.
    auto tree = Column(Text("aaaa bbbb").fontSize(10).padding(20).setKey("t")).width(100);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 200);

    // What measure reserved: 2 lines * 10 + 40 vertical padding.
    CHECK(root->children[0]->layout.height == doctest::Approx(60));

    render::renderTree(root, backend, {});

    REQUIRE(backend.count(Call::Kind::TextRun) == 2);
    const Call* r0 = backend.nthOf(Call::Kind::TextRun, 0);
    const Call* r1 = backend.nthOf(Call::Kind::TextRun, 1);
    CHECK(r0->text == "aaaa");
    CHECK(r0->x == doctest::Approx(20));  // content origin, inset by the padding
    CHECK(r0->y == doctest::Approx(20));
    CHECK(r1->text == "bbbb");
    CHECK(r1->x == doctest::Approx(20));
    CHECK(r1->y == doctest::Approx(30));  // content origin + one lineHeight
}

TEST_CASE("renderTree: wrap(false) text draws exactly one run") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree =
        Column(Text("aaa bbb").fontSize(10).wrap(false).setKey("t")).width(40).alignItems(AlignItems::FlexStart);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(40, 100);

    render::renderTree(root, backend, {});

    REQUIRE(backend.count(Call::Kind::TextRun) == 1);
    const Call* run = backend.nthOf(Call::Kind::TextRun, 0);
    CHECK(run->text == "aaa bbb");  // the full single-line run, overflow and all
    CHECK(run->y == doctest::Approx(0));
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

    // Root box has no chrome; the stream is clip / content / unclip, then the
    // vertical scrollbar (track + thumb) in its reserved gutter, outside the
    // clip. The clip is the VIEWPORT: content width minus the gutter.
    REQUIRE(backend.calls.size() == 5);

    CHECK(backend.calls[0].kind == Call::Kind::PushClip);
    checkRect(backend.calls[0].rect, 10, 10, 100 - rd::kScrollbarThickness, 80);

    CHECK(backend.calls[1].kind == Call::Kind::FillRect);
    checkRect(backend.calls[1].rect, 10, -20, 50, 200);  // y = 10 - 30 scroll offset

    CHECK(backend.calls[2].kind == Call::Kind::PopClip);
    CHECK(backend.count(Call::Kind::PushClip) == backend.count(Call::Kind::PopClip));

    // Track fills the gutter at the viewport's right edge; the thumb is
    // trackLen * vh/content = 80 * 80/200 = 32 tall, at offset 30 of
    // maxScroll 120 over 48 of travel. (The fixed 50-wide content never
    // overflowed horizontally, so only the vertical gutter is reserved.)
    CHECK(backend.calls[3].kind == Call::Kind::FillRect);
    CHECK(backend.calls[3].color == rd::kScrollbarTrackColor);
    checkRect(backend.calls[3].rect, 10 + 100 - rd::kScrollbarThickness, 10, rd::kScrollbarThickness, 80);
    CHECK(backend.calls[4].kind == Call::Kind::FillRect);
    CHECK(backend.calls[4].color == rd::kScrollbarThumbColor);
    checkRect(backend.calls[4].rect, 10 + 100 - rd::kScrollbarThickness, 10 + (30.0f / 120.0f) * 48,
              rd::kScrollbarThickness, 32);
}

TEST_CASE("renderTree: scroll padding insets the clip viewport and the content origin") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    // The unsized child stretches to the scroll's VIEWPORT width: the padded
    // content box (100 - 2*10) minus the vertical gutter its 200px height
    // reserves — Scroll padding AND the gutter shrink what the detached
    // content root lays out against.
    auto tree =
        Scroll(Box().height(200).backgroundColor(0x112233FFu).setKey("content")).width(100).height(80).padding(10);

    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 80);

    REQUIRE(root->type() == PrimitiveType::Scroll);
    auto* scroll = static_cast<ScrollNode*>(root);
    scroll->scrollOffsetY = 30;

    render::renderTree(root, backend, {});

    REQUIRE(backend.calls.size() == 5);

    CHECK(backend.calls[0].kind == Call::Kind::PushClip);
    checkRect(backend.calls[0].rect, 10, 10, 80 - rd::kScrollbarThickness, 60);  // the VIEWPORT

    CHECK(backend.calls[1].kind == Call::Kind::FillRect);
    checkRect(backend.calls[1].rect, 10, -20, 80 - rd::kScrollbarThickness,
              200);  // content origin (10,10) - 30 scroll

    CHECK(backend.calls[2].kind == Call::Kind::PopClip);

    // The bar fills its gutter just past the viewport's right edge (x =
    // 10 + 72), flush against the right padding band, spanning the 60px
    // viewport height. The proportional thumb (60 * 60/200 = 18) clamps up to
    // the min length; offset 30 of maxScroll 140 maps over the remaining travel.
    float thumbLen = rd::kScrollbarMinThumbLen;
    float travel = 60 - thumbLen;
    CHECK(backend.calls[3].color == rd::kScrollbarTrackColor);
    checkRect(backend.calls[3].rect, 10 + 80 - rd::kScrollbarThickness, 10, rd::kScrollbarThickness, 60);
    CHECK(backend.calls[4].color == rd::kScrollbarThumbColor);
    checkRect(backend.calls[4].rect, 10 + 80 - rd::kScrollbarThickness, 10 + (30.0f / 140.0f) * travel,
              rd::kScrollbarThickness, thumbLen);
}

// ---------------------------------------------------------------------------
// Overlay scrollbars: a bar per overflowing axis, proportional thumb tracking
// the offset, min-length clamp, corner yield when both bars show, and no bar
// at all when content fits.
// ---------------------------------------------------------------------------

TEST_CASE("renderTree: vertical scrollbar thumb is proportional and tracks the offset") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    // Content 2x the viewport: thumb = trackLen * vh/content = 100 * 100/200 = 50.
    auto tree = Scroll(Box().width(50).height(200).setKey("content")).width(100).height(100);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 100);
    auto* scroll = static_cast<ScrollNode*>(root);

    render::renderTree(root, backend, {});

    // No content chrome: the only fills are the track and the thumb.
    REQUIRE(backend.count(Call::Kind::FillRect) == 2);
    const Call* track = backend.nthOf(Call::Kind::FillRect, 0);
    const Call* thumb = backend.nthOf(Call::Kind::FillRect, 1);
    CHECK(track->color == rd::kScrollbarTrackColor);
    checkRect(track->rect, 100 - rd::kScrollbarThickness, 0, rd::kScrollbarThickness, 100);
    CHECK(thumb->color == rd::kScrollbarThumbColor);
    checkRect(thumb->rect, 100 - rd::kScrollbarThickness, 0, rd::kScrollbarThickness, 50);  // top at scroll 0

    // At max scroll the thumb's far edge meets the track's far edge.
    scroll->scrollOffsetY = 100;  // maxScroll = 200 - 100
    backend.calls.clear();
    render::renderTree(root, backend, {});
    checkRect(backend.nthOf(Call::Kind::FillRect, 1)->rect, 100 - rd::kScrollbarThickness, 50,
              rd::kScrollbarThickness, 50);
}

TEST_CASE("renderTree: no scrollbar when content fits the viewport") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Scroll(Box().width(50).height(50).backgroundColor(0x112233FFu).setKey("content"))
                    .width(100)
                    .height(100);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 100);

    render::renderTree(root, backend, {});

    CHECK(backend.count(Call::Kind::FillRect) == 1);  // the content box only
}

TEST_CASE("renderTree: both bars when both axes overflow; tracks yield the shared corner") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Scroll(Box().width(200).height(200).setKey("content")).width(100).height(100);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 100);

    render::renderTree(root, backend, {});

    // Vertical track+thumb, then horizontal track+thumb. Both gutters are
    // reserved, so each track spans its viewport axis (100 - t) and stops
    // short of the shared corner by construction — the corner square is the
    // intersection of the two gutters. Thumbs are proportional to the reduced
    // viewport: trackLen * viewport/content = (100 - t) * (100 - t) / 200.
    REQUIRE(backend.count(Call::Kind::FillRect) == 4);
    float t = rd::kScrollbarThickness;
    float thumbLen = (100 - t) * (100 - t) / 200;
    checkRect(backend.nthOf(Call::Kind::FillRect, 0)->rect, 100 - t, 0, t, 100 - t);
    checkRect(backend.nthOf(Call::Kind::FillRect, 1)->rect, 100 - t, 0, t, thumbLen);
    checkRect(backend.nthOf(Call::Kind::FillRect, 2)->rect, 0, 100 - t, 100 - t, t);
    checkRect(backend.nthOf(Call::Kind::FillRect, 3)->rect, 0, 100 - t, thumbLen, t);
}

TEST_CASE("renderTree: tiny proportional thumb clamps to the min length and still reaches the end") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    // 1000px of content in a 100px viewport: proportional thumb would be 10px,
    // below the grabbable minimum.
    auto tree = Scroll(Box().width(50).height(1000).setKey("content")).width(100).height(100);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 100);
    auto* scroll = static_cast<ScrollNode*>(root);
    scroll->scrollOffsetY = 900;  // max

    render::renderTree(root, backend, {});

    const Call* thumb = backend.nthOf(Call::Kind::FillRect, 1);
    CHECK(thumb->rect.h == doctest::Approx(rd::kScrollbarMinThumbLen));
    // At max scroll the clamped thumb still lands flush with the track end.
    CHECK(thumb->rect.y + thumb->rect.h == doctest::Approx(100));
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

    // Chrome outside the clip; the text run inside a clip of the content box
    // (== the border box here — no insets).
    REQUIRE(backend.calls.size() == 5);

    CHECK(backend.calls[0].kind == Call::Kind::FillRect);
    CHECK(backend.calls[0].color == rd::kInputBg);
    CHECK(backend.calls[0].radius == doctest::Approx(rd::kInputBorderRadius));
    checkRect(backend.calls[0].rect, 0, 0, 100, 30);

    CHECK(backend.calls[1].kind == Call::Kind::StrokeRect);
    CHECK(backend.calls[1].color == rd::kInputBorder);
    CHECK(backend.calls[1].strokeWidth == doctest::Approx(rd::kInputBorderWidth));

    CHECK(backend.calls[2].kind == Call::Kind::PushClip);
    checkRect(backend.calls[2].rect, 0, 0, 100, 30);

    CHECK(backend.calls[3].kind == Call::Kind::TextRun);
    CHECK(backend.calls[3].text == "abc");
    CHECK(backend.calls[3].x == doctest::Approx(rd::kInputTextPad));
    CHECK(backend.calls[3].y == doctest::Approx((30 - 10) / 2.0f));  // (h - fontSize) / 2
    CHECK(backend.calls[3].color == rd::kDefaultTextColor);

    CHECK(backend.calls[4].kind == Call::Kind::PopClip);
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

TEST_CASE("renderTree: password mask is one star per code point, not per byte") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    // "aé" is 3 bytes but 2 code points ('é' = 0xC3 0xA9). Per-byte masking
    // drew 3 stars; the mask must be per code point.
    auto tree = Input().value("a\xC3\xA9").password(true).fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);
    root->focused = true;  // fresh blink state: caret starts visible
    auto* in = static_cast<InputNode*>(root);
    in->caret = in->displayText.size();  // pin at END (the caret index defaults to 0)
    in->clearSelection();                // the edit path always collapses the anchor too

    render::renderTree(root, backend, {});

    const Call* run = backend.nthOf(Call::Kind::TextRun, 0);
    REQUIRE(run != nullptr);
    CHECK(run->text == "**");

    // The caret measures the MASKED run: pad + "**" (2 bytes * 10px), centered
    // on the caret width — it sits at the end of the stars, not the raw bytes.
    const Call* caret = backend.nthOf(Call::Kind::FillRect, 1);
    REQUIRE(caret != nullptr);
    CHECK(caret->rect.x == doctest::Approx(rd::kInputTextPad + 20.0f - rd::kCaretWidth / 2));
}

TEST_CASE("renderTree: non-password input shows multibyte text unmasked") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Input().value("a\xC3\xA9").fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);

    render::renderTree(root, backend, {});

    const Call* run = backend.nthOf(Call::Kind::TextRun, 0);
    REQUIRE(run != nullptr);
    CHECK(run->text == "a\xC3\xA9");
}

// ---------------------------------------------------------------------------
// Caret: a fillRect past the measured run when focused and the blink phase says
// visible; never present when unfocused. Blink is node state driven by
// update(dt), so the tests step the phase deterministically — no wall clock.
// ---------------------------------------------------------------------------

TEST_CASE("renderTree: focused input draws the caret; blink phase hides it deterministically") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Input().value("abc").fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);
    root->focused = true;  // fresh blink state: caret starts visible
    auto* in = static_cast<InputNode*>(root);
    in->caret = in->displayText.size();  // pin at END (the caret index defaults to 0)
    in->clearSelection();                // the edit path always collapses the anchor too

    render::renderTree(root, backend, {});

    // bg fill + caret fill: the caret is the second FillRect, drawn last
    // inside the content clip (only the closing PopClip follows it).
    REQUIRE(backend.count(Call::Kind::FillRect) == 2);
    const Call* caret = backend.nthOf(Call::Kind::FillRect, 1);
    REQUIRE(caret != nullptr);
    CHECK(caret == &backend.calls[backend.calls.size() - 2]);
    CHECK(backend.calls.back().kind == Call::Kind::PopClip);

    // x: pad + measured "abc" (3 chars * 10px), centered on the caret width.
    float caretX = rd::kInputTextPad + 30.0f - rd::kCaretWidth / 2;
    checkRect(caret->rect, caretX, rd::kCaretInset, rd::kCaretWidth, 30 - 2 * rd::kCaretInset);
    CHECK(caret->color == rd::kDefaultTextColor);
    CHECK(caret->radius == doctest::Approx(0));

    // Step the blink phase past the on-window (530ms of a 1000ms period): the
    // caret hides, so only the background fill remains.
    root->update(0.6f);
    backend.calls.clear();
    render::renderTree(root, backend, {});
    CHECK(backend.count(Call::Kind::FillRect) == 1);

    // Step across the period boundary: visible again.
    root->update(0.6f);  // phase 1200ms -> 200ms, inside the on-window
    backend.calls.clear();
    render::renderTree(root, backend, {});
    CHECK(backend.count(Call::Kind::FillRect) == 2);
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
    root->focused = true;  // fresh blink state: caret visible

    render::renderTree(root, backend, {});

    REQUIRE(backend.count(Call::Kind::FillRect) == 2);
    const Call* caret = backend.nthOf(Call::Kind::FillRect, 1);
    REQUIRE(caret != nullptr);
    CHECK(caret->rect.x == doctest::Approx(rd::kInputTextPad - rd::kCaretWidth / 2));  // no advance past "hint"
}

// ---------------------------------------------------------------------------
// Caret at index: the caret draws at pad + the measured DISPLAY PREFIX before
// its byte offset — not pinned to the end of the run. For a password the
// prefix is one star per code point (star space).
// ---------------------------------------------------------------------------

TEST_CASE("renderTree: caret draws at the measured prefix before its byte index") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Input().value("abc").fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);
    root->focused = true;  // fresh blink state: caret visible
    auto* in = static_cast<InputNode*>(root);

    // FakeBackend measures 10px per byte, so caret-x is pad + 10 * caret.
    auto caretXAt = [&](size_t caret) {
        in->caret = caret;
        in->clearSelection();  // the edit path always collapses the anchor too
        backend.calls.clear();
        render::renderTree(root, backend, {});
        const Call* c = backend.nthOf(Call::Kind::FillRect, 1);
        REQUIRE(c != nullptr);
        return c->rect.x;
    };

    CHECK(caretXAt(0) == doctest::Approx(rd::kInputTextPad - rd::kCaretWidth / 2));
    CHECK(caretXAt(1) == doctest::Approx(rd::kInputTextPad + 10.0f - rd::kCaretWidth / 2));
    CHECK(caretXAt(2) == doctest::Approx(rd::kInputTextPad + 20.0f - rd::kCaretWidth / 2));
    // At the end the prefix is the whole run — the pre-caret-index geometry.
    CHECK(caretXAt(3) == doctest::Approx(rd::kInputTextPad + 30.0f - rd::kCaretWidth / 2));
}

TEST_CASE("renderTree: password caret positions in star space (code points, not bytes)") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    // "aéb": boundaries at 0, 1, 3, 4 — but the DISPLAY run is "***" (one star
    // per code point), so the caret advances one 10px star per code point.
    auto tree = Input().value("a\xC3\xA9" "b").password(true).fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);
    root->focused = true;  // fresh blink state: caret visible
    auto* in = static_cast<InputNode*>(root);

    auto caretXAt = [&](size_t caret) {
        in->caret = caret;
        in->clearSelection();  // the edit path always collapses the anchor too
        backend.calls.clear();
        render::renderTree(root, backend, {});
        const Call* c = backend.nthOf(Call::Kind::FillRect, 1);
        REQUIRE(c != nullptr);
        return c->rect.x;
    };

    CHECK(caretXAt(0) == doctest::Approx(rd::kInputTextPad - rd::kCaretWidth / 2));
    CHECK(caretXAt(1) == doctest::Approx(rd::kInputTextPad + 10.0f - rd::kCaretWidth / 2));  // 1 cp -> 1 star
    CHECK(caretXAt(3) == doctest::Approx(rd::kInputTextPad + 20.0f - rd::kCaretWidth / 2));  // past é: 2 stars
    CHECK(caretXAt(4) == doctest::Approx(rd::kInputTextPad + 30.0f - rd::kCaretWidth / 2));  // end: 3 stars
}

// ---------------------------------------------------------------------------
// Input clip + follow-scroll: text and caret draw between a pushClip/popClip
// pair over the input's content box; when the caret would overflow, the edit
// path's textScrollX shifts BOTH the run and the caret left so the caret stays
// inside; the clip stack stays balanced across every display variant.
// ---------------------------------------------------------------------------

TEST_CASE("renderTree: overflowing input text and caret are clipped to the content box") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    // 12 chars * 10px = 120px of text in a 100px box: the run overflows.
    auto tree = Input().value("aaaaaaaaaaaa").fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);
    root->focused = true;  // fresh blink state: caret visible
    auto* in = static_cast<InputNode*>(root);
    in->caret = in->displayText.size();  // at END: the prefix is the full 120px run
    in->clearSelection();                // the edit path always collapses the anchor too
    // What the edit path does after every caret move: follow-scroll so the
    // caret sits at the right edge of the visible span (content minus the
    // text pad both sides = 84px): textScrollX = 120 - 84 = 36.
    in->scrollCaretIntoView(&backend);
    CHECK(in->textScrollX == doctest::Approx(36));

    render::renderTree(root, backend, {});

    // bg fill / border stroke / clip(content box) / text run / caret fill / unclip.
    REQUIRE(backend.calls.size() == 6);
    CHECK(backend.calls[2].kind == Call::Kind::PushClip);
    checkRect(backend.calls[2].rect, 0, 0, 100, 30);  // no insets: content == border box
    CHECK(backend.calls[3].kind == Call::Kind::TextRun);
    CHECK(backend.calls[4].kind == Call::Kind::FillRect);  // the caret, inside the clip
    CHECK(backend.calls[5].kind == Call::Kind::PopClip);
    CHECK(backend.count(Call::Kind::PushClip) == backend.count(Call::Kind::PopClip));

    // Both shift left by textScrollX: the run starts at pad - 36 (its head
    // clipped away), and the caret lands INSIDE the box at pad + 120 - 36
    // (was pinned to the right edge before follow-scroll existed).
    CHECK(backend.calls[3].x == doctest::Approx(rd::kInputTextPad - 36));
    CHECK(backend.calls[4].rect.x == doctest::Approx(rd::kInputTextPad + 120 - 36 - rd::kCaretWidth / 2));
}

TEST_CASE("renderTree: textScrollX zero leaves short-text geometry untouched") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    // Text fits: the follow-scroll clamps to 0 even after a caret move to the
    // end, so the pre-scroll geometry (run at the pad, caret past the run) is
    // byte-identical to the no-scroll rendering.
    auto tree = Input().value("abc").fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);
    root->focused = true;
    auto* in = static_cast<InputNode*>(root);
    in->caret = in->displayText.size();
    in->clearSelection();  // the edit path always collapses the anchor too
    in->scrollCaretIntoView(&backend);
    CHECK(in->textScrollX == doctest::Approx(0));

    render::renderTree(root, backend, {});
    const Call* run = backend.nthOf(Call::Kind::TextRun, 0);
    REQUIRE(run != nullptr);
    CHECK(run->x == doctest::Approx(rd::kInputTextPad));
    const Call* caret = backend.nthOf(Call::Kind::FillRect, 1);
    REQUIRE(caret != nullptr);
    CHECK(caret->rect.x == doctest::Approx(rd::kInputTextPad + 30 - rd::kCaretWidth / 2));
}

TEST_CASE("renderTree: input clip stays balanced across display variants") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    SUBCASE("placeholder, unfocused: one pair around the placeholder run") {
        auto tree = Input().placeholder("hint").fontSize(10).width(100).height(30);
        auto* root = h.mount(std::move(tree));
        root->calculateLayout(100, 30);
        render::renderTree(root, backend, {});
        CHECK(backend.count(Call::Kind::PushClip) == 1);
        CHECK(backend.count(Call::Kind::TextRun) == 1);
    }

    SUBCASE("empty, unfocused: nothing to draw inside the box, no clip at all") {
        auto tree = Input().fontSize(10).width(100).height(30);
        auto* root = h.mount(std::move(tree));
        root->calculateLayout(100, 30);
        render::renderTree(root, backend, {});
        CHECK(backend.count(Call::Kind::PushClip) == 0);
        CHECK(backend.count(Call::Kind::TextRun) == 0);
    }

    SUBCASE("empty, focused: one pair around the caret alone") {
        auto tree = Input().fontSize(10).width(100).height(30);
        auto* root = h.mount(std::move(tree));
        root->calculateLayout(100, 30);
        root->focused = true;
        render::renderTree(root, backend, {});
        CHECK(backend.count(Call::Kind::PushClip) == 1);
        CHECK(backend.count(Call::Kind::TextRun) == 0);
        CHECK(backend.count(Call::Kind::FillRect) == 2);  // bg + caret
    }

    CHECK(backend.count(Call::Kind::PushClip) == backend.count(Call::Kind::PopClip));
}

TEST_CASE("renderTree: padded input clips and positions text in its content box") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Input().value("abc").fontSize(10).width(100).height(40).padding(5);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 40);

    render::renderTree(root, backend, {});

    const Call* clip = backend.nthOf(Call::Kind::PushClip, 0);
    REQUIRE(clip != nullptr);
    checkRect(clip->rect, 5, 5, 90, 30);  // border box minus the 5px insets

    const Call* run = backend.nthOf(Call::Kind::TextRun, 0);
    REQUIRE(run != nullptr);
    CHECK(run->x == doctest::Approx(5 + rd::kInputTextPad));
    CHECK(run->y == doctest::Approx(5 + (30 - 10) / 2.0f));  // centered in the content box
}

// ---------------------------------------------------------------------------
// Selection highlight (6c C3): a kSelectionColor fill behind the selected
// range, drawn inside the content clip between the box chrome and the text
// run. Both edges measure through the SAME prefix source as the caret
// (InputNode::prefixWidthAt — star space for a password), shift by the same
// follow-scroll, and occupy the caret's vertical band.
// ---------------------------------------------------------------------------

TEST_CASE("renderTree: selection draws a highlight between chrome and the text run") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Input().value("abcdef").fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);
    root->focused = true;  // fresh blink state: caret visible
    auto* in = static_cast<InputNode*>(root);
    in->selectionAnchor = 1;
    in->caret = 4;  // the moving end

    render::renderTree(root, backend, {});

    // bg fill / border stroke / clip / selection fill / text run / caret fill / unclip.
    REQUIRE(backend.calls.size() == 7);
    CHECK(backend.calls[2].kind == Call::Kind::PushClip);
    CHECK(backend.calls[3].kind == Call::Kind::FillRect);  // highlight under the run
    CHECK(backend.calls[4].kind == Call::Kind::TextRun);
    CHECK(backend.calls[5].kind == Call::Kind::FillRect);  // caret over it
    CHECK(backend.calls[6].kind == Call::Kind::PopClip);

    // The highlight spans prefix(1)=10 .. prefix(4)=40 in the caret band.
    const Call& sel = backend.calls[3];
    checkRect(sel.rect, rd::kInputTextPad + 10, rd::kCaretInset, 30, 30 - 2 * rd::kCaretInset);
    CHECK(sel.color == rd::kSelectionColor);

    // The caret draws at the MOVING end (byte 4) and shares the band exactly.
    const Call& caret = backend.calls[5];
    CHECK(caret.rect.x == doctest::Approx(rd::kInputTextPad + 40 - rd::kCaretWidth / 2));
    CHECK(caret.rect.y == doctest::Approx(sel.rect.y));
    CHECK(caret.rect.h == doctest::Approx(sel.rect.h));
}

TEST_CASE("renderTree: a right-to-left selection draws the same rect; caret at the left end") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Input().value("abcdef").fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);
    root->focused = true;
    auto* in = static_cast<InputNode*>(root);
    in->selectionAnchor = 4;
    in->caret = 1;  // extended leftward: the moving end is the LEFT edge

    render::renderTree(root, backend, {});

    // Same [1,4) highlight as the left-to-right selection...
    const Call* sel = backend.nthOf(Call::Kind::FillRect, 1);
    REQUIRE(sel != nullptr);
    CHECK(sel->color == rd::kSelectionColor);
    checkRect(sel->rect, rd::kInputTextPad + 10, rd::kCaretInset, 30, 30 - 2 * rd::kCaretInset);

    // ... but the caret marks the left end (byte 1), where the user is dragging.
    const Call* caret = backend.nthOf(Call::Kind::FillRect, 2);
    REQUIRE(caret != nullptr);
    CHECK(caret->rect.x == doctest::Approx(rd::kInputTextPad + 10 - rd::kCaretWidth / 2));
}

TEST_CASE("renderTree: no selection draws no highlight — the C2 stream unchanged") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Input().value("abcdef").fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);
    root->focused = true;
    auto* in = static_cast<InputNode*>(root);
    in->caret = 4;
    in->selectionAnchor = 4;  // anchor == caret: no selection

    render::renderTree(root, backend, {});

    // bg fill / border stroke / clip / text run / caret fill / unclip — exactly
    // the pre-selection stream, and nothing painted in the selection color.
    REQUIRE(backend.calls.size() == 6);
    CHECK(backend.count(Call::Kind::FillRect) == 2);  // bg + caret only
    for (const auto& c : backend.calls)
        CHECK(c.color != rd::kSelectionColor);
}

TEST_CASE("renderTree: an unfocused input with a selection draws no highlight") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    // Same selection state as the focused case — only focus differs. The
    // highlight is cursor chrome (like the caret): blur keeps the selection
    // state but hides its chrome, so a blurred input shows no blue band.
    auto tree = Input().value("abcdef").fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);
    auto* in = static_cast<InputNode*>(root);
    in->selectionAnchor = 1;
    in->caret = 4;
    REQUIRE(in->hasSelection());

    SUBCASE("unfocused: no highlight and no caret — just the box chrome") {
        // (root->focused stays false)
        render::renderTree(root, backend, {});
        CHECK(backend.count(Call::Kind::FillRect) == 1);  // bg only
        for (const auto& c : backend.calls)
            CHECK(c.color != rd::kSelectionColor);
    }

    SUBCASE("refocus restores the chrome: the highlight draws again") {
        root->focused = true;
        render::renderTree(root, backend, {});
        const Call* sel = backend.nthOf(Call::Kind::FillRect, 1);
        REQUIRE(sel != nullptr);
        CHECK(sel->color == rd::kSelectionColor);
    }
}

TEST_CASE("renderTree: password selection highlights star-space widths") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    // Raw "aéb" displays "***". Selecting [1,4) (é+b) covers stars 2..3:
    // x = pad + 10 (one star before), width = 20 (two stars).
    auto tree = Input().value("a\xC3\xA9" "b").password(true).fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);
    root->focused = true;
    auto* in = static_cast<InputNode*>(root);
    in->selectionAnchor = 1;
    in->caret = 4;

    render::renderTree(root, backend, {});

    const Call* sel = backend.nthOf(Call::Kind::FillRect, 1);
    REQUIRE(sel != nullptr);
    CHECK(sel->color == rd::kSelectionColor);
    CHECK(sel->rect.x == doctest::Approx(rd::kInputTextPad + 10));
    CHECK(sel->rect.w == doctest::Approx(20));
}

TEST_CASE("renderTree: the selection highlight shifts by the follow-scroll with run and caret") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    // 12 chars * 10px = 120px in a 100px box (span 84). Everything selected,
    // caret at the end: the follow-scroll is 36 and the highlight rides left
    // with the run (its head clipped away by the content clip).
    auto tree = Input().value("aaaaaaaaaaaa").fontSize(10).width(100).height(30);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(100, 30);
    root->focused = true;
    auto* in = static_cast<InputNode*>(root);
    in->selectionAnchor = 0;
    in->caret = 12;
    in->scrollCaretIntoView(&backend);
    CHECK(in->textScrollX == doctest::Approx(36));

    render::renderTree(root, backend, {});

    const Call* sel = backend.nthOf(Call::Kind::FillRect, 1);
    REQUIRE(sel != nullptr);
    CHECK(sel->color == rd::kSelectionColor);
    CHECK(sel->rect.x == doctest::Approx(rd::kInputTextPad - 36));  // prefix(0) - scroll
    CHECK(sel->rect.w == doctest::Approx(120));

    // The caret (moving end) sits inside the box at pad + 120 - 36.
    const Call* caret = backend.nthOf(Call::Kind::FillRect, 2);
    REQUIRE(caret != nullptr);
    CHECK(caret->rect.x == doctest::Approx(rd::kInputTextPad + 120 - 36 - rd::kCaretWidth / 2));
}

// ---------------------------------------------------------------------------
// Multiline input (6c C6): the wrapped-run loop. Lines stack TOP-aligned from
// the content top (single-line centers its one run — a documented divergence),
// the selection paints one rect per overlapped line, the caret occupies its
// line's box, and textScrollY shifts the whole block up inside the clip.
// ---------------------------------------------------------------------------

TEST_CASE("renderTree: multiline input draws its wrapped runs top-aligned in a loop") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    // Box-wrapped so the input keeps its own rect at the origin; the input
    // auto-grows to its 2 hard lines (measure func).
    auto tree = Box(
                    Input().value("ab\ncd").multiline().fontSize(10).width(100)
                )
                    .width(200)
                    .height(100);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(200, 100);
    auto* in = static_cast<InputNode*>(root->children[0].get());
    CHECK(in->layout.height == doctest::Approx(20));

    render::renderTree(root, backend, {});

    // One run per line at the text pad, stacked by lineHeight from the top —
    // NOT the single-line vertically-centered y.
    REQUIRE(backend.count(Call::Kind::TextRun) == 2);
    const Call* run0 = backend.nthOf(Call::Kind::TextRun, 0);
    const Call* run1 = backend.nthOf(Call::Kind::TextRun, 1);
    CHECK(run0->text == "ab");
    CHECK(run0->x == doctest::Approx(rd::kInputTextPad));
    CHECK(run0->y == doctest::Approx(0));
    CHECK(run1->text == "cd");
    CHECK(run1->x == doctest::Approx(rd::kInputTextPad));
    CHECK(run1->y == doctest::Approx(10));
    CHECK(backend.count(Call::Kind::PushClip) == backend.count(Call::Kind::PopClip));
}

TEST_CASE("renderTree: a multiline selection paints one rect per overlapped line") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    // Runs [0,4), [5,9), [10,14). Selection [2,12) overlaps all three:
    // partial first, full middle, partial last.
    auto tree = Box(
                    Input().value("aaaa\nbbbb\ncccc").multiline().fontSize(10).width(100)
                )
                    .width(200)
                    .height(100);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(200, 100);
    auto* in = static_cast<InputNode*>(root->children[0].get());
    in->focused = true;
    in->selectionAnchor = 2;
    in->caret = 12;

    render::renderTree(root, backend, {});

    // Stream: bg fill / border stroke / clip / 3 selection fills / 3 text
    // runs / caret fill / unclip — highlight under the glyphs, caret on top.
    REQUIRE(backend.calls.size() == 11);
    CHECK(backend.calls[2].kind == Call::Kind::PushClip);
    for (int i = 3; i <= 5; ++i)
        CHECK(backend.calls[i].kind == Call::Kind::FillRect);
    for (int i = 6; i <= 8; ++i)
        CHECK(backend.calls[i].kind == Call::Kind::TextRun);
    CHECK(backend.calls[9].kind == Call::Kind::FillRect);
    CHECK(backend.calls[10].kind == Call::Kind::PopClip);

    // Partial first line: [2,4) of "aaaa" -> x = pad + 20, width 20, line 0.
    CHECK(backend.calls[3].color == rd::kSelectionColor);
    checkRect(backend.calls[3].rect, rd::kInputTextPad + 20, 0, 20, 10);
    // Fully-enclosed middle line: its whole run width, full line box.
    CHECK(backend.calls[4].color == rd::kSelectionColor);
    checkRect(backend.calls[4].rect, rd::kInputTextPad, 10, 40, 10);
    // Partial last line: [10,12) -> from the line start, width 20.
    CHECK(backend.calls[5].color == rd::kSelectionColor);
    checkRect(backend.calls[5].rect, rd::kInputTextPad, 20, 20, 10);

    // The caret (moving end, byte 12) sits on line 2 at column 20.
    const Call& caret = backend.calls[9];
    CHECK(caret.rect.x == doctest::Approx(rd::kInputTextPad + 20 - rd::kCaretWidth / 2));
    CHECK(caret.rect.y == doctest::Approx(20));
    CHECK(caret.rect.h == doctest::Approx(10));
}

TEST_CASE("renderTree: a multiline selection within one line paints a single rect") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Box(
                    Input().value("aaaa\nbbbb").multiline().fontSize(10).width(100)
                )
                    .width(200)
                    .height(100);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(200, 100);
    auto* in = static_cast<InputNode*>(root->children[0].get());
    in->focused = true;
    in->selectionAnchor = 1;
    in->caret = 3;

    render::renderTree(root, backend, {});

    int selRects = 0;
    for (const auto& c : backend.calls)
        if (c.kind == Call::Kind::FillRect && c.color == rd::kSelectionColor)
            ++selRects;
    CHECK(selRects == 1);
    const Call* sel = backend.nthOf(Call::Kind::FillRect, 1);  // bg is fill 0
    REQUIRE(sel != nullptr);
    checkRect(sel->rect, rd::kInputTextPad + 10, 0, 20, 10);
}

TEST_CASE("renderTree: textScrollY shifts multiline runs and caret up inside the clip") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    // Five 10px lines in a 25px box; caret at the end follows to scrollY 25.
    auto tree = Box(
                    Input().value("a\nb\nc\nd\ne").multiline().fontSize(10).width(100).height(25)
                )
                    .width(200)
                    .height(100);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(200, 100);
    auto* in = static_cast<InputNode*>(root->children[0].get());
    in->focused = true;
    in->caret = in->displayText.size();
    in->clearSelection();
    in->scrollCaretIntoView(&backend);
    CHECK(in->textScrollY == doctest::Approx(25));

    render::renderTree(root, backend, {});

    // The first line rides 25px above the content top (clipped away); the
    // last sits at 40 - 25 = 15; the caret shares its line's y.
    const Call* clip = backend.nthOf(Call::Kind::PushClip, 0);
    REQUIRE(clip != nullptr);
    checkRect(clip->rect, 0, 0, 100, 25);
    const Call* run0 = backend.nthOf(Call::Kind::TextRun, 0);
    CHECK(run0->y == doctest::Approx(-25));
    const Call* run4 = backend.nthOf(Call::Kind::TextRun, 4);
    CHECK(run4->y == doctest::Approx(15));
    const Call* caret = backend.nthOf(Call::Kind::FillRect, 1);
    REQUIRE(caret != nullptr);
    CHECK(caret->rect.y == doctest::Approx(15));
    CHECK(backend.count(Call::Kind::PushClip) == backend.count(Call::Kind::PopClip));
}

TEST_CASE("renderTree: empty multiline input draws the placeholder run and a first-line caret") {
    FakeBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);

    auto tree = Box(
                    Input().placeholder("hint").multiline().fontSize(10).width(100)
                )
                    .width(200)
                    .height(100);
    auto* root = h.mount(std::move(tree));
    root->calculateLayout(200, 100);
    auto* in = static_cast<InputNode*>(root->children[0].get());
    in->focused = true;  // fresh blink state: caret visible

    render::renderTree(root, backend, {});

    // Placeholder: one un-wrapped run in the placeholder color at the first
    // line slot; the caret sits at the pad on line 0 (no advance past "hint").
    REQUIRE(backend.count(Call::Kind::TextRun) == 1);
    const Call* run = backend.nthOf(Call::Kind::TextRun, 0);
    CHECK(run->text == "hint");
    CHECK(run->color == rd::kPlaceholderColor);
    CHECK(run->y == doctest::Approx(0));
    const Call* caret = backend.nthOf(Call::Kind::FillRect, 1);
    REQUIRE(caret != nullptr);
    CHECK(caret->rect.x == doctest::Approx(rd::kInputTextPad - rd::kCaretWidth / 2));
    CHECK(caret->rect.y == doctest::Approx(0));
    CHECK(caret->rect.h == doctest::Approx(10));
    CHECK(backend.count(Call::Kind::PushClip) == backend.count(Call::Kind::PopClip));
}
