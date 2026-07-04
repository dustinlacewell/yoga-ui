#include "doctest.h"

#include <yui/render/TreeRenderer.hpp>
#include <yui/yui.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

using namespace yui;

namespace {

// A recording IRenderBackend (the test_portal/test_tree_renderer idiom): every
// primitive call becomes a Call record so tests assert on the exact draw
// stream — that the modal's scrim and dialog paint at root z, after any clip.
struct Call {
    enum class Kind { FillRect, StrokeRect, PushClip, PopClip, TextRun, Canvas };
    explicit Call(Kind k) : kind(k) {}
    Kind kind;
    render::Rect rect{};
    uint32_t color = 0;
};

class RecordingBackend : public render::IRenderBackend {
public:
    std::vector<Call> calls;

    float measureRun(std::string_view run, float /*fontSize*/, std::string_view /*font*/) const override {
        return static_cast<float>(run.size()) * 10.0f;
    }
    FontMetrics fontMetrics(float fontSize, std::string_view /*font*/) const override {
        return {0.8f * fontSize, 0.2f * fontSize, fontSize};
    }

    void beginFrame() override {}
    void endFrame() override {}

    void fillRect(const render::Rect& r, uint32_t color, float /*radius*/) override {
        Call c(Call::Kind::FillRect);
        c.rect = r;
        c.color = color;
        calls.push_back(c);
    }
    void strokeRect(const render::Rect& r, uint32_t color, float /*radius*/, float /*width*/) override {
        Call c(Call::Kind::StrokeRect);
        c.rect = r;
        c.color = color;
        calls.push_back(c);
    }
    void pushClip(const render::Rect& r, float /*radius*/) override {
        Call c(Call::Kind::PushClip);
        c.rect = r;
        calls.push_back(c);
    }
    void popClip() override { calls.push_back(Call(Call::Kind::PopClip)); }
    void drawTextRun(const std::string& /*run*/, float x, float y, float /*fontSize*/, uint32_t color,
                     std::string_view /*font*/) override {
        Call c(Call::Kind::TextRun);
        c.rect = {x, y, 0, 0};
        c.color = color;
        calls.push_back(c);
    }
    void drawCanvas(const CanvasNode& /*node*/, const render::Rect& r) override {
        Call c(Call::Kind::Canvas);
        c.rect = r;
        calls.push_back(c);
    }

    // Index of the first FillRect with this color, or -1.
    int fillIndex(uint32_t color) const {
        for (size_t i = 0; i < calls.size(); ++i)
            if (calls[i].kind == Call::Kind::FillRect && calls[i].color == color)
                return static_cast<int>(i);
        return -1;
    }
    // Index of the last PopClip, or -1.
    int lastPopIndex() const {
        for (int i = static_cast<int>(calls.size()) - 1; i >= 0; --i)
            if (calls[static_cast<size_t>(i)].kind == Call::Kind::PopClip)
                return i;
        return -1;
    }
};

void checkRect(const render::Rect& r, float x, float y, float w, float h) {
    CHECK(r.x == doctest::Approx(x));
    CHECK(r.y == doctest::Approx(y));
    CHECK(r.w == doctest::Approx(w));
    CHECK(r.h == doctest::Approx(h));
}

// First node with this key in pre-order (reconciles shuffle child indexes, so
// tests re-find by key rather than navigating by position).
Node* findByKey(Node* node, const std::string& key) {
    if (node->key == key)
        return node;
    for (auto& child : node->children)
        if (Node* found = findByKey(child.get(), key))
            return found;
    return nullptr;
}

// One press+release at a point through the full Host entry points.
bool clickAt(Host& host, float x, float y) {
    host.handleMouseDown(x, y, MouseButton::Left);
    return host.handleMouseUp(x, y, MouseButton::Left);
}

}  // namespace

// ---------------------------------------------------------------------------
// Input blocking: the modal's full-viewport backdrop is portal content, hit-
// tested ABOVE the main tree, so a click aimed at a button behind the modal
// lands on the backdrop instead — and dismisses, if enabled.
// ---------------------------------------------------------------------------

TEST_CASE("Modal backdrop blocks click-through to the tree behind; backdrop click dismisses") {
    Host host;
    int buttonClicks = 0;
    int dismissCount = 0;
    std::function<void(bool)> setOpen;

    auto App = [&](ComponentContext& ctx) -> VNode {
        auto [open, setter] = ctx.useState<bool>(true);
        setOpen = setter;
        std::vector<Child> kids;
        kids.push_back(Box().width(100).height(50).onClick([&] { buttonClicks++; }).setKey("btn"));
        if (open) {
            kids.push_back(
                widgets::Modal(Box().width(200).height(100).setKey("dialog")).onDismiss([&] { dismissCount++; }));
        }
        return Box(std::move(kids)).width(800).height(600);
    };
    host.setRender(Component(App));
    host.update(800, 600);

    // Click where the main-tree button sits (its top-left corner region).
    CHECK(clickAt(host, 50, 25));  // consumed by the backdrop
    CHECK(buttonClicks == 0);      // never reached the button behind
    CHECK(dismissCount == 1);      // the backdrop click IS the dismiss affordance

    // Close the modal: the same click reaches the button again (regression).
    setOpen(false);
    host.update(800, 600);
    clickAt(host, 50, 25);
    CHECK(buttonClicks == 1);
    CHECK(dismissCount == 1);
}

TEST_CASE("Modal panel click does not dismiss; a click just outside the centered dialog does") {
    Host host;
    int dismissCount = 0;

    auto App = [&](ComponentContext& ctx) -> VNode {
        (void)ctx;
        // Dialog 200x100 centered in 800x600 -> (300,250)-(500,350).
        return Box(widgets::Modal(Box().width(200).height(100).setKey("dialog")).onDismiss([&] { dismissCount++; }))
            .width(800)
            .height(600);
    };
    host.setRender(Component(App));
    host.update(800, 600);

    // Inside the dialog: the panel's consuming onClick stops the bubble to
    // its parent backdrop — no dismiss.
    CHECK(clickAt(host, 400, 300));
    CHECK(dismissCount == 0);

    // Just outside the dialog: the backdrop is hit directly — dismiss. (Also
    // pins the centering: (250,300) is left of the dialog's 300px edge.)
    CHECK(clickAt(host, 250, 300));
    CHECK(dismissCount == 1);
}

TEST_CASE("Modal dismissOnBackdropClick=false: backdrop click neither dismisses nor falls through") {
    Host host;
    int buttonClicks = 0;
    int dismissCount = 0;

    auto App = [&](ComponentContext& ctx) -> VNode {
        (void)ctx;
        std::vector<Child> kids;
        kids.push_back(Box().width(100).height(50).onClick([&] { buttonClicks++; }).setKey("btn"));
        kids.push_back(widgets::Modal(Box().width(200).height(100).setKey("dialog"))
                           .onDismiss([&] { dismissCount++; })
                           .dismissOnBackdropClick(false));
        return Box(std::move(kids)).width(800).height(600);
    };
    host.setRender(Component(App));
    host.update(800, 600);

    // The backdrop's no-op click still consumes: blocked, not dismissed.
    CHECK(clickAt(host, 50, 25));
    CHECK(dismissCount == 0);
    CHECK(buttonClicks == 0);
}

// ---------------------------------------------------------------------------
// dismissKeyCode: an app-supplied raw platform keycode. The trap keeps focus
// inside the modal, so keys route to the focused node and bubble to the
// panel's onKeyDown — the matching key dismisses, everything else is consumed
// there and never reaches app hotkeys behind the modal.
// ---------------------------------------------------------------------------

TEST_CASE("Modal dismissKeyCode: matching key dismisses; other keys are consumed at the modal") {
    constexpr int kEsc = 27;  // "the app's Esc code" — yui never interprets it
    constexpr int kOther = 65;

    Host host;
    int dismissCount = 0;
    int hotkeyCount = 0;
    std::function<void(bool)> setOpen;

    auto App = [&](ComponentContext& ctx) -> VNode {
        auto [open, setter] = ctx.useState<bool>(true);
        setOpen = setter;
        std::vector<Child> kids;
        if (open) {
            kids.push_back(widgets::Modal(Box().width(200).height(100).focusable().autoFocus().setKey("t1"))
                               .dismissKeyCode(kEsc)
                               .onDismiss([&] { dismissCount++; }));
        }
        return Box(std::move(kids)).width(800).height(600).onKeyDown([&](int, uint16_t, bool) {
            hotkeyCount++;
        });  // app-root hotkey
    };
    host.setRender(Component(App));
    host.update(800, 600);
    REQUIRE(host.getFocusedNode() == findByKey(host.root(), "t1"));  // trap+autoFocus (F2)

    // Non-matching key: consumed by the panel, no dismiss, no app hotkey.
    CHECK(host.handleKeyDown(kOther, KeyMod_None));
    CHECK(dismissCount == 0);
    CHECK(hotkeyCount == 0);

    // Matching key: dismiss.
    CHECK(host.handleKeyDown(kEsc, KeyMod_None));
    CHECK(dismissCount == 1);

    // Control: with the modal closed the app-root hotkey is reachable again.
    setOpen(false);
    host.update(800, 600);
    host.handleKeyDown(kOther, KeyMod_None);
    CHECK(hotkeyCount == 1);
}

// ---------------------------------------------------------------------------
// Focus (the F2 integration, re-asserted at the Modal level): opening traps
// and saves, autoFocus focuses inside, Tab cycles only the panel's
// focusables, closing restores the pre-modal focus. All from .trapFocus() —
// the Modal wires nothing.
// ---------------------------------------------------------------------------

TEST_CASE("Modal focus: open traps and autofocuses, Tab cycles the panel only, close restores") {
    Host host;
    std::function<void(bool)> setOpen;

    auto App = [&](ComponentContext& ctx) -> VNode {
        auto [open, setter] = ctx.useState<bool>(false);
        setOpen = setter;
        std::vector<Child> kids;
        kids.push_back(Box().width(50).height(50).focusable().setKey("out"));
        if (open) {
            kids.push_back(widgets::Modal(Box().width(20).height(20).focusable().autoFocus().setKey("t1"),
                                          Box().width(20).height(20).focusable().setKey("t2")));
        }
        return Box(std::move(kids)).width(800).height(600);
    };
    host.setRender(Component(App));
    host.update(800, 600);

    Node* out = findByKey(host.root(), "out");
    host.focus(out);
    CHECK(host.getFocusedNode() == out);

    // Open: the trap-mount peek saves "out" BEFORE content mounts, then the
    // content autoFocus moves focus inside.
    setOpen(true);
    host.update(800, 600);
    Node* t1 = findByKey(host.root(), "t1");
    Node* t2 = findByKey(host.root(), "t2");
    REQUIRE(t1 != nullptr);
    REQUIRE(t2 != nullptr);
    CHECK(host.getFocusedNode() == t1);

    // Tab is scoped to the modal: t1 <-> t2, "out" unreachable.
    host.focusNext();
    CHECK(host.getFocusedNode() == t2);
    host.focusNext();
    CHECK(host.getFocusedNode() == t1);  // wrapped inside the modal
    host.focusPrev();
    CHECK(host.getFocusedNode() == t2);

    // Close (unmount): focus restores to the saved pre-modal node.
    setOpen(false);
    host.update(800, 600);
    CHECK(host.getFocusedNode() == findByKey(host.root(), "out"));
}

// ---------------------------------------------------------------------------
// Wheel blocking: hit-order keeps scroll off content BEHIND the modal, and
// the backdrop's consuming onScroll stops the bubble through the portal into
// the LOGICAL parent chain — here, a Scroll the modal is declared inside.
// ---------------------------------------------------------------------------

TEST_CASE("Modal blocks the wheel from a Scroll around its logical position") {
    Host host;
    std::function<void(bool)> setOpen;

    auto App = [&](ComponentContext& ctx) -> VNode {
        auto [open, setter] = ctx.useState<bool>(true);
        setOpen = setter;
        std::vector<Child> scrollKids;
        scrollKids.push_back(Box().width(100).height(1000));  // overflows: scrollable
        if (open) {
            scrollKids.push_back(widgets::Modal(Box().width(200).height(100).setKey("dialog")));
        }
        return Box(Scroll(std::move(scrollKids)).width(200).height(200).setKey("scroll")).width(800).height(600);
    };
    host.setRender(Component(App));
    host.update(800, 600);

    auto* scroll = static_cast<ScrollNode*>(findByKey(host.root(), "scroll"));
    REQUIRE(scroll != nullptr);

    // Wheel over the scroll while the modal is open: the backdrop catches and
    // consumes it — the scroll does not move.
    CHECK(host.handleScroll(100, 100, 0, -50));
    CHECK(scroll->targetScrollY == doctest::Approx(0));

    // Control: with the modal closed the same wheel scrolls.
    setOpen(false);
    host.update(800, 600);
    host.handleScroll(100, 100, 0, -50);
    CHECK(scroll->targetScrollY == doctest::Approx(50));
}

// ---------------------------------------------------------------------------
// Root z / clip escape (the F1 contract, re-asserted through the Modal's
// composition): a modal opened from inside a clipped container paints scrim
// then dialog AFTER the main walk's last clip pop, at viewport coordinates,
// and the dialog is clickable at its centered position far outside the clip.
// ---------------------------------------------------------------------------

TEST_CASE("Modal renders at root z and escapes an ancestor clip") {
    constexpr uint32_t colScrim = 0x00000088u;  // the widget's default backdrop
    constexpr uint32_t colDialog = 0xDD0000FFu;

    Host host;
    RecordingBackend backend;
    int dialogClicks = 0;

    auto App = [&](ComponentContext& ctx) -> VNode {
        (void)ctx;
        // The modal's logical parent is a tiny Scroll at the top-left; its
        // content must still cover and center against the whole viewport.
        return Box(Scroll(Box().width(80).height(200), widgets::Modal(Box()
                                                                          .width(200)
                                                                          .height(100)
                                                                          .backgroundColor(colDialog)
                                                                          .onClick([&] { dialogClicks++; })
                                                                          .setKey("dialog")))
                       .width(100)
                       .height(50))
            .width(800)
            .height(600);
    };
    host.setRender(Component(App));
    host.update(800, 600);
    render::renderTree(host.root(), backend, {});

    // Scrim and dialog paint after the scroll's clip is popped (deferred
    // portal pass on a clean clip stack), scrim below dialog.
    int scrimIdx = backend.fillIndex(colScrim);
    int dialogIdx = backend.fillIndex(colDialog);
    REQUIRE(scrimIdx >= 0);
    REQUIRE(dialogIdx >= 0);
    CHECK(scrimIdx > backend.lastPopIndex());
    CHECK(dialogIdx > scrimIdx);

    // Root-space geometry: scrim fills the viewport, dialog centers in it —
    // not in the 100x50 scroll.
    checkRect(backend.calls[static_cast<size_t>(scrimIdx)].rect, 0, 0, 800, 600);
    checkRect(backend.calls[static_cast<size_t>(dialogIdx)].rect, 300, 250, 200, 100);

    // And the dialog is hittable there, far outside the scroll's clip rect.
    CHECK(clickAt(host, 400, 300));
    CHECK(dialogClicks == 1);
}

// ---------------------------------------------------------------------------
// Stacked modals: B (declared after A) layers above A — B's backdrop blocks
// A's dialog — Tab is scoped to B, and closing B restores focus per the F2
// single-slot rule (to the OUTERMOST save) while A stays open and clickable.
// ---------------------------------------------------------------------------

TEST_CASE("Stacked modals: B's backdrop blocks A's dialog; close restores outermost focus") {
    Host host;
    std::function<void(bool)> setA, setB;
    int dismissA = 0;
    int dismissB = 0;
    int aDialogClicks = 0;

    auto App = [&](ComponentContext& ctx) -> VNode {
        auto [a, setterA] = ctx.useState<bool>(false);
        auto [b, setterB] = ctx.useState<bool>(false);
        setA = setterA;
        setB = setterB;
        std::vector<Child> kids;
        kids.push_back(Box().width(50).height(50).focusable().setKey("out"));
        if (a) {
            // A's dialog 400x300 centered -> (200,150)-(600,450).
            kids.push_back(
                widgets::Modal(
                    Box().width(400).height(300).onClick([&] { aDialogClicks++; }).focusable().autoFocus().setKey("a1"))
                    .onDismiss([&] { dismissA++; }));
        }
        if (b) {
            // B's dialog 100x100 centered -> (350,250)-(450,350).
            kids.push_back(
                widgets::Modal(Box().width(100).height(100).focusable().autoFocus().setKey("b1")).onDismiss([&] {
                    dismissB++;
                }));
        }
        return Box(std::move(kids)).width(800).height(600);
    };
    host.setRender(Component(App));
    host.update(800, 600);

    host.focus(findByKey(host.root(), "out"));
    setA(true);
    host.update(800, 600);
    CHECK(host.getFocusedNode() == findByKey(host.root(), "a1"));
    setB(true);
    host.update(800, 600);
    CHECK(host.getFocusedNode() == findByKey(host.root(), "b1"));

    // Two portals, document order A then B (paint back-to-front; hit reverse).
    std::vector<Node*> portals;
    collectPortals(host.root(), portals);
    REQUIRE(portals.size() == 2);

    // (250,300) is inside A's dialog but outside B's: B's backdrop (topmost)
    // catches it — B dismisses, A's dialog never sees the click.
    CHECK(clickAt(host, 250, 300));
    CHECK(dismissB == 1);
    CHECK(dismissA == 0);
    CHECK(aDialogClicks == 0);

    // Tab is scoped to B: its sole focusable wraps onto itself.
    host.focusNext();
    CHECK(host.getFocusedNode() == findByKey(host.root(), "b1"));
    host.focusNext();
    CHECK(host.getFocusedNode() == findByKey(host.root(), "b1"));

    // Close B: the single saved slot restores straight to "out" (outermost
    // rule) — and A is back on top, its dialog clickable at the same point.
    setB(false);
    host.update(800, 600);
    CHECK(host.getFocusedNode() == findByKey(host.root(), "out"));
    CHECK(clickAt(host, 250, 300));
    CHECK(aDialogClicks == 1);
    CHECK(dismissA == 0);
}
