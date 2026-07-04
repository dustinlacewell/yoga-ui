#include "TestMeasurer.hpp"
#include "doctest.h"

#include <yui/core/EventHandler.hpp>
#include <yui/core/NodeRef.hpp>
#include <yui/render/TreeRenderer.hpp>
#include <yui/yui.hpp>

#include <string>
#include <string_view>
#include <vector>

using namespace yui;
using yui::test::MeasureHarness;

namespace {

// A recording IRenderBackend (the test_tree_renderer FakeBackend idiom): every
// primitive call becomes a Call record so tests assert on the exact draw
// stream — portal deferral order, clip balance — with no real backend.
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

    int count(Call::Kind k) const {
        int n = 0;
        for (const auto& c : calls)
            if (c.kind == k)
                ++n;
        return n;
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

// Mirror the Host ctor's focus wiring on a headless harness: node removal
// clears the event slots, autoFocus routes to focusNode, and a mounted
// .trapFocus() portal traps with save=true.
void wireFocus(MeasureHarness& h, EventHandler& events) {
    h.reconciler().setNodeRemovedCallback([&events](Node* n) { events.onNodeRemoved(n); });
    h.reconciler().setAutoFocusCallback([&events](Node* n) { events.focusNode(n); });
    h.reconciler().setTrapMountedCallback(
        [&events](Node* n) { events.setFocusTrap(n, /*save=*/true); });
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

}  // namespace

// ---------------------------------------------------------------------------
// Clip escape: a Portal declared INSIDE a Scroll paints its content after the
// main walk (no active clip) and is hittable at its root-space position even
// though that position is far outside the scroll's clip rect.
// ---------------------------------------------------------------------------

TEST_CASE("Portal content escapes the Scroll clip: drawn after the main walk, hittable outside") {
    RecordingBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);
    EventHandler events;

    bool panelClicked = false;
    bool insideClicked = false;
    auto tree = Box(
                    Scroll(
                        Box().width(80).height(40).backgroundColor(0x111111FFu).setKey("inside")
                            .onClick([&] { insideClicked = true; }),
                        Portal(
                            Box().positionType(PositionType::Absolute)
                                .positionLeft(200).positionTop(100)
                                .width(50).height(40)
                                .backgroundColor(0x222222FFu)
                                .onClick([&] { panelClicked = true; })
                                .setKey("panel"))
                    ).width(100).height(50).setKey("scroll")
                ).width(300).height(300);

    auto* root = h.mount(std::move(tree));
    root->calculateLayout(300, 300);
    render::renderTree(root, backend, {});

    // Exact stream: the scroll's clip push, the inside box, the pop — then the
    // portal panel, AFTER the last pop (the deferred pass runs on a clean clip
    // stack; nothing clips it).
    REQUIRE(backend.calls.size() == 4);
    CHECK(backend.calls[0].kind == Call::Kind::PushClip);
    CHECK(backend.calls[1].kind == Call::Kind::FillRect);
    CHECK(backend.calls[1].color == 0x111111FFu);
    CHECK(backend.calls[2].kind == Call::Kind::PopClip);
    CHECK(backend.calls[3].kind == Call::Kind::FillRect);
    CHECK(backend.calls[3].color == 0x222222FFu);
    checkRect(backend.calls[3].rect, 200, 100, 50, 40);

    // Clip balance: the portal draws must not leak a push.
    CHECK(backend.count(Call::Kind::PushClip) == backend.count(Call::Kind::PopClip));

    // Hittable at the root-space position, outside the scroll's rect entirely.
    Node* scroll = root->children[0].get();
    Node* portal = scroll->children[1].get();
    REQUIRE(portal->type() == PrimitiveType::Portal);
    Node* panel = portal->children[0].get();
    CHECK(events.topmostHit(root, 225, 120) == panel);

    events.handleMouseDown(root, 225, 120);
    events.handleMouseUp(root, 225, 120);
    CHECK(panelClicked);
    CHECK(!insideClicked);

    // Content inside the scroll still hits normally (regression).
    CHECK(events.topmostHit(root, 10, 10) == scroll->children[0].get());
}

// ---------------------------------------------------------------------------
// Layering discipline: paint order == collectPortals == hit order, including a
// portal nested inside another portal's content ({A contains C, B} -> A, B, C).
// ---------------------------------------------------------------------------

TEST_CASE("Portal layering: paint order == collectPortals == topmostHit order (nested portal)") {
    constexpr uint32_t colRoot = 0x01010101u;
    constexpr uint32_t colA = 0xAAAAAAAAu;
    constexpr uint32_t colB = 0xBBBBBBBBu;
    constexpr uint32_t colC = 0xCCCCCCCCu;

    RecordingBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);
    EventHandler events;

    // PortalA's content contains PortalC; PortalB follows A in document order.
    // The three 100x100 panels overlap in a staircase so hit points can pick
    // out each layer.
    auto tree = Box(
                    Portal(
                        Box(
                            Portal(
                                Box().positionType(PositionType::Absolute)
                                    .positionLeft(140).positionTop(140)
                                    .width(100).height(100)
                                    .backgroundColor(colC).setKey("C"))
                        ).positionType(PositionType::Absolute)
                            .positionLeft(100).positionTop(100)
                            .width(100).height(100)
                            .backgroundColor(colA).setKey("A")),
                    Portal(
                        Box().positionType(PositionType::Absolute)
                            .positionLeft(120).positionTop(120)
                            .width(100).height(100)
                            .backgroundColor(colB).setKey("B"))
                ).width(400).height(400).backgroundColor(colRoot);

    auto* root = h.mount(std::move(tree));
    root->calculateLayout(400, 400);

    Node* portalA = root->children[0].get();
    Node* portalB = root->children[1].get();
    Node* boxA = portalA->children[0].get();
    Node* portalC = boxA->children[0].get();
    Node* boxB = portalB->children[0].get();
    Node* boxC = portalC->children[0].get();

    // The ONE shared order: A, B (document order), then C (nested inside A's
    // content, appended after every main-tree portal).
    std::vector<Node*> portals;
    collectPortals(root, portals);
    REQUIRE(portals.size() == 3);
    CHECK(portals[0] == portalA);
    CHECK(portals[1] == portalB);
    CHECK(portals[2] == portalC);

    // Paint: main content first, then the portals in exactly collectPortals
    // order — asserted against the SAME list, not a parallel expectation.
    render::renderTree(root, backend, {});
    int rootIdx = backend.fillIndex(colRoot);
    int aIdx = backend.fillIndex(colA);
    int bIdx = backend.fillIndex(colB);
    int cIdx = backend.fillIndex(colC);
    REQUIRE(rootIdx >= 0);
    REQUIRE(aIdx >= 0);
    REQUIRE(bIdx >= 0);
    REQUIRE(cIdx >= 0);
    CHECK(rootIdx < aIdx);  // portals above main content
    CHECK(aIdx < bIdx);     // portals[0] before portals[1]
    CHECK(bIdx < cIdx);     // portals[1] before portals[2]

    // Hit: topmost-first — the reverse of the paint order, from the same list.
    CHECK(events.topmostHit(root, 150, 150) == boxC);  // under A, B, C -> C wins
    CHECK(events.topmostHit(root, 130, 130) == boxB);  // under A, B -> B wins
    CHECK(events.topmostHit(root, 110, 110) == boxA);  // under A only
    CHECK(events.topmostHit(root, 50, 50) == root);    // no portal -> main tree
}

// ---------------------------------------------------------------------------
// Liveness: unmounting the portal's LOGICAL parent removes the content, clears
// the hover/press/focus slots pointing into it, and leaves no dangling pointer
// — a click at the old position falls to the main tree, no UAF.
// ---------------------------------------------------------------------------

TEST_CASE("Portal removal: logical-parent unmount clears slots, later input is a safe no-op") {
    RecordingBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);
    EventHandler events;
    // Mirror the Host wiring: node removal clears the event slots.
    h.reconciler().setNodeRemovedCallback([&](Node* n) { events.onNodeRemoved(n); });

    auto build = [](bool withPortal) {
        std::vector<Child> children;
        children.push_back(Box().width(50).height(50).setKey("main"));
        if (withPortal) {
            children.push_back(
                Box(Portal(
                        Box().positionType(PositionType::Absolute)
                            .positionLeft(200).positionTop(200)
                            .width(80).height(60)
                            .backgroundColor(0x33333333u)
                            .focusable()
                            .setKey("panel")))
                    .setKey("holder"));
        }
        return VNode(Box(std::move(children)).width(400).height(400));
    };

    auto* root = h.mount(build(true));
    root->calculateLayout(400, 400);

    Node* holder = root->children[1].get();
    Node* portal = holder->children[0].get();
    REQUIRE(portal->type() == PrimitiveType::Portal);
    Node* panel = portal->children[0].get();
    std::shared_ptr<bool> panelAlive = panel->alive;

    // Point hover, press, and focus INTO the portal content.
    events.handleMouseMove(root, 240, 230);
    CHECK(events.getHoveredNode() == panel);
    events.handleMouseDown(root, 240, 230);  // press (capture) + click-focus
    CHECK(events.getFocusedNode() == panel);
    CHECK(events.hasCapture());

    // Unmount the logical parent (the portal + content ride the subtree
    // removal; no stored portal pointer exists anywhere to go stale).
    h.reconciler().reconcile(h.fiber(), build(false));
    CHECK(*panelAlive == false);  // the content node is genuinely freed

    // onNodeRemoved cleared every slot that pointed into portal content.
    CHECK(events.getHoveredNode() == nullptr);
    CHECK(events.getFocusedNode() == nullptr);
    CHECK(events.hasCapture() == false);

    // Input at the old position is a safe no-op that falls to the main tree.
    root->calculateLayout(400, 400);
    CHECK_NOTHROW(events.handleMouseUp(root, 240, 230));
    CHECK_NOTHROW(events.handleMouseMove(root, 240, 230));
    CHECK(events.topmostHit(root, 240, 230) == root);
    CHECK_NOTHROW(events.handleMouseDown(root, 240, 230));
    CHECK_NOTHROW(events.handleMouseUp(root, 240, 230));

    // And the paint pass draws no portal content anymore.
    backend.calls.clear();
    render::renderTree(root, backend, {});
    CHECK(backend.fillIndex(0x33333333u) == -1);
}

// ---------------------------------------------------------------------------
// Detached layout (V1): percent sizes on portal content resolve against the
// VIEWPORT passed to calculateLayout, not the logical parent's box.
// ---------------------------------------------------------------------------

TEST_CASE("Portal content: percent sizes resolve against the viewport") {
    MeasureHarness h;

    // Logical parent is 300x200 — the 640x480 result proves content sized
    // against the viewport, not the parent.
    auto tree = Box(Portal(Box().widthPercent(100).heightPercent(100).setKey("fill")))
                    .width(300).height(200);

    auto* root = h.mount(std::move(tree));
    root->calculateLayout(640, 480);

    Node* portal = root->children[0].get();
    REQUIRE(portal->type() == PrimitiveType::Portal);
    Node* fill = portal->children[0].get();
    CHECK(fill->layout.width == doctest::Approx(640));
    CHECK(fill->layout.height == doctest::Approx(480));
}

// ---------------------------------------------------------------------------
// Zero-flow (V2): the Portal node contributes NOTHING to its logical parent's
// layout — no size, no gap slot — even with user padding/margin props applied.
// ---------------------------------------------------------------------------

TEST_CASE("Portal is zero-flow in its logical parent, even with padding/margin props") {
    MeasureHarness h;

    // Column with a gap: if the portal occupied a flow slot (even a zero-size
    // one), the gaps flanking it would push B to 30+10+0+10 = 50. Zero-flow
    // means B sits at 30 + one gap = 40, exactly as if the portal weren't
    // declared. The padding/margin props exercise the display:none RE-FORCE
    // in PortalNode::updateProps (applyLayoutProps resets display).
    auto tree = Column(
                    Box().width(50).height(30).setKey("A"),
                    Portal(Box().width(10).height(10)).padding(10).margin(5),
                    Box().width(50).height(30).setKey("B"))
                    .width(200).height(200).gap(10);

    auto* root = h.mount(std::move(tree));
    root->calculateLayout(200, 200);

    Node* a = root->children[0].get();
    Node* portal = root->children[1].get();
    Node* b = root->children[2].get();
    REQUIRE(portal->type() == PrimitiveType::Portal);
    CHECK(a->layout.top == doctest::Approx(0));
    CHECK(b->layout.top == doctest::Approx(40));
    CHECK(portal->layout.width == doctest::Approx(0));
    CHECK(portal->layout.height == doctest::Approx(0));
}

// ---------------------------------------------------------------------------
// Root-space placement: content at positionLeft/Top draws and hits at exactly
// that position regardless of where the portal sits in the logical tree, and
// absoluteRect (getBoundingRect) stops accumulating at the portal boundary.
// ---------------------------------------------------------------------------

TEST_CASE("Portal content places in root space; absoluteRect breaks at the portal boundary") {
    constexpr uint32_t colP = 0x44444444u;

    RecordingBackend backend;
    MeasureHarness h;
    h.setMeasurer(&backend);
    EventHandler events;

    // The portal sits inside a logical wrapper offset to (37, 23); its content
    // must land at (200, 100) root-space, NOT (237, 123).
    auto tree = Box(
                    Box(
                        Portal(
                            Box().positionType(PositionType::Absolute)
                                .positionLeft(200).positionTop(100)
                                .width(50).height(40)
                                .backgroundColor(colP).setKey("panel"))
                    ).positionType(PositionType::Absolute)
                        .positionLeft(37).positionTop(23)
                        .width(100).height(100).setKey("wrapper")
                ).width(400).height(400);

    auto* root = h.mount(std::move(tree));
    root->calculateLayout(400, 400);
    render::renderTree(root, backend, {});

    int pIdx = backend.fillIndex(colP);
    REQUIRE(pIdx >= 0);
    checkRect(backend.calls[static_cast<size_t>(pIdx)].rect, 200, 100, 50, 40);

    Node* wrapper = root->children[0].get();
    Node* portal = wrapper->children[0].get();
    Node* panel = portal->children[0].get();

    // Hittable exactly where it draws; the double-counted position misses.
    CHECK(events.topmostHit(root, 225, 120) == panel);
    CHECK(events.topmostHit(root, 262, 143) != panel);

    // absoluteRect stops at the Portal ancestor: the content's accumulated
    // coords are already root-absolute — no wrapper offsets folded in.
    layout::Rect r = absoluteRect(panel);
    CHECK(r.x == doctest::Approx(200));
    CHECK(r.y == doctest::Approx(100));
    CHECK(r.w == doctest::Approx(50));
    CHECK(r.h == doctest::Approx(40));
}

// ---------------------------------------------------------------------------
// trapFocus (F2): the declarative trap. Applied by the reconciler's mount peek
// BEFORE the portal's content mounts (so the focus save precedes any content
// autoFocus), scoping Tab to the content; clearing the trap — explicitly or by
// unmounting the portal — restores the saved focus.
// ---------------------------------------------------------------------------

TEST_CASE("Portal trapFocus: Tab cycles only the portal content, never outside") {
    MeasureHarness h;
    EventHandler events;
    wireFocus(h, events);

    auto tree = Box(
                    Box().width(50).height(50).focusable().setKey("out"),
                    Portal(
                        Box(
                            Box().width(20).height(20).focusable().setKey("t1"),
                            Box().width(20).height(20).focusable().setKey("t2")
                        ).setKey("panel"))
                        .trapFocus()
                ).width(400).height(400);

    auto* root = h.mount(std::move(tree));
    Node* t1 = findByKey(root, "t1");
    Node* t2 = findByKey(root, "t2");
    REQUIRE(t1 != nullptr);
    REQUIRE(t2 != nullptr);

    // Traversal is scoped to the portal (the trap root): Tab enters at the
    // first content focusable and wraps inside — "out" is unreachable.
    events.focusNext(root);
    CHECK(events.getFocusedNode() == t1);
    events.focusNext(root);
    CHECK(events.getFocusedNode() == t2);
    events.focusNext(root);
    CHECK(events.getFocusedNode() == t1);  // wraps inside the trap
    events.focusPrev(root);
    CHECK(events.getFocusedNode() == t2);
}

TEST_CASE("Portal trapFocus: pre-portal focus saved before content autoFocus; unmount restores") {
    MeasureHarness h;
    EventHandler events;
    wireFocus(h, events);

    auto build = [](bool withModal) {
        std::vector<Child> children;
        children.push_back(Box().width(50).height(50).focusable().setKey("out"));
        if (withModal) {
            children.push_back(
                Portal(Box().width(80).height(60).focusable().autoFocus().setKey("modal-box"))
                    .trapFocus());
        }
        return VNode(Box(std::move(children)).width(400).height(400));
    };

    auto* root = h.mount(build(false));
    Node* out = findByKey(root, "out");
    REQUIRE(out != nullptr);
    events.focusNode(out);
    CHECK(events.getFocusedNode() == out);

    // Mount the modal: the trap peek fires BEFORE the content mounts, so the
    // save captures "out"; the content autoFocus then moves focus inside.
    h.reconciler().reconcile(h.fiber(), build(true));
    Node* modalBox = findByKey(root, "modal-box");
    REQUIRE(modalBox != nullptr);
    CHECK(events.getFocusedNode() == modalBox);  // autoFocus won mount focus
    CHECK(findByKey(root, "out") == out);        // keyed sibling was reused

    // Unmount the modal. Had the save happened AFTER the autoFocus, it would
    // hold the (now freed) modal box and restore nothing — restoring to "out"
    // proves the peek ordering.
    h.reconciler().reconcile(h.fiber(), build(false));
    CHECK(events.getFocusedNode() == out);
}

TEST_CASE("Portal trapFocus: explicit clearFocusTrap restores the saved focus and unscopes Tab") {
    MeasureHarness h;
    EventHandler events;
    wireFocus(h, events);

    auto build = [](bool withModal) {
        std::vector<Child> children;
        children.push_back(Box().width(50).height(50).focusable().setKey("out"));
        if (withModal) {
            children.push_back(
                Portal(Box().width(80).height(60).focusable().setKey("t1")).trapFocus());
        }
        return VNode(Box(std::move(children)).width(400).height(400));
    };

    auto* root = h.mount(build(false));
    Node* out = findByKey(root, "out");
    events.focusNode(out);

    // Trap mounts (no content autoFocus): focus stays on "out", the save holds it.
    h.reconciler().reconcile(h.fiber(), build(true));
    CHECK(events.getFocusedNode() == out);

    // Tab is scoped: it pulls focus into the trap.
    Node* t1 = findByKey(root, "t1");
    events.focusNext(root);
    CHECK(events.getFocusedNode() == t1);

    // Explicit clear restores the saved focus...
    events.clearFocusTrap();
    CHECK(events.getFocusedNode() == out);

    // ...and unscopes traversal: from "out", full document order reaches t1,
    // then wraps back to "out" (under the trap, t1 wrapped onto itself).
    events.focusNext(root);
    CHECK(events.getFocusedNode() == t1);
    events.focusNext(root);
    CHECK(events.getFocusedNode() == out);
}

TEST_CASE("Portal trapFocus: saved node removed while the modal is open -> no restore, no crash") {
    MeasureHarness h;
    EventHandler events;
    wireFocus(h, events);

    auto build = [](bool withOut, bool withModal) {
        std::vector<Child> children;
        if (withOut)
            children.push_back(Box().width(50).height(50).focusable().setKey("out"));
        if (withModal) {
            children.push_back(
                Portal(Box().width(80).height(60).focusable().autoFocus().setKey("modal-box"))
                    .trapFocus());
        }
        return VNode(Box(std::move(children)).width(400).height(400));
    };

    auto* root = h.mount(build(true, false));
    events.focusNode(findByKey(root, "out"));

    SUBCASE("eager path: onNodeRemoved drops the save when the saved node goes") {
        h.reconciler().reconcile(h.fiber(), build(true, true));  // modal opens, saves "out"
        h.reconciler().reconcile(h.fiber(), build(false, true)); // "out" removed mid-modal
        CHECK(events.getFocusedNode() == findByKey(root, "modal-box"));

        h.reconciler().reconcile(h.fiber(), build(false, false));  // modal closes
        CHECK(events.getFocusedNode() == nullptr);  // nothing to restore to
        CHECK_NOTHROW(events.focusNext(root));      // and traversal stays safe
    }

    SUBCASE("token path: a save freed with no onNodeRemoved degrades to no restore") {
        h.reconciler().reconcile(h.fiber(), build(true, true));  // modal opens, saves "out"

        // Simulate a reconciliation freeing the saved node WITHOUT the removal
        // callback (the exact hole the weak liveness token exists for), then
        // rewire for the close.
        h.reconciler().setNodeRemovedCallback({});
        h.reconciler().reconcile(h.fiber(), build(false, true));
        h.reconciler().setNodeRemovedCallback([&events](Node* n) { events.onNodeRemoved(n); });

        h.reconciler().reconcile(h.fiber(), build(false, false));  // modal closes
        CHECK(events.getFocusedNode() == nullptr);  // dead token: no restore, no UAF
        CHECK_NOTHROW(events.focusNext(root));
    }
}

TEST_CASE("Portal trapFocus: nested traps restore to the OUTERMOST saved focus") {
    MeasureHarness h;
    EventHandler events;
    wireFocus(h, events);

    auto build = [](bool withA, bool withB) {
        std::vector<Child> children;
        children.push_back(Box().width(50).height(50).focusable().setKey("out"));
        if (withA) {
            children.push_back(
                Portal(Box().width(80).height(60).focusable().autoFocus().setKey("a1"))
                    .trapFocus());
        }
        if (withB) {
            children.push_back(
                Portal(Box().width(80).height(60).focusable().autoFocus().setKey("b1"))
                    .trapFocus());
        }
        return VNode(Box(std::move(children)).width(400).height(400));
    };

    auto* root = h.mount(build(false, false));
    Node* out = findByKey(root, "out");
    events.focusNode(out);

    // Modal A opens: saves "out", focuses a1.
    h.reconciler().reconcile(h.fiber(), build(true, false));
    CHECK(events.getFocusedNode() == findByKey(root, "a1"));

    // Modal B opens on top: the single saved slot keeps the OUTERMOST save
    // ("out") — B's trap-with-save finds a live save and records nothing.
    h.reconciler().reconcile(h.fiber(), build(true, true));
    CHECK(events.getFocusedNode() == findByKey(root, "b1"));

    // Closing B (the current trap root) restores straight to "out" — the
    // ratified v1 single-slot rule: no trap stack, the inner close restores
    // to the outermost save and clears the trap entirely.
    h.reconciler().reconcile(h.fiber(), build(true, false));
    CHECK(events.getFocusedNode() == out);

    // Closing A finds no trap and no save left: focus stays on "out".
    h.reconciler().reconcile(h.fiber(), build(false, false));
    CHECK(events.getFocusedNode() == out);
}

TEST_CASE("Portal without trapFocus: no trap, no save — Tab reaches content AND outside") {
    MeasureHarness h;
    EventHandler events;
    wireFocus(h, events);

    auto tree = Box(
                    Box().width(50).height(50).focusable().setKey("out"),
                    Portal(Box().width(80).height(60).focusable().setKey("p1"))
                ).width(400).height(400);

    auto* root = h.mount(std::move(tree));
    Node* out = findByKey(root, "out");
    Node* p1 = findByKey(root, "p1");

    // Full-tree traversal: outside and portal content alternate — no scoping.
    events.focusNext(root);
    CHECK(events.getFocusedNode() == out);
    events.focusNext(root);
    CHECK(events.getFocusedNode() == p1);
    events.focusNext(root);
    CHECK(events.getFocusedNode() == out);
}
