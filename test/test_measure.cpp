#include "doctest.h"

#include "TestMeasurer.hpp"

#include <yui/core/Host.hpp>

#include <functional>

using namespace yui;
using yui::test::FnMeasurer;
using yui::test::MeasureHarness;

namespace {

// 10px per character, fontSize for height — the canonical test measurer.
FnMeasurer::Fn perChar(float pxPerChar) {
    return [pxPerChar](const std::string& text, float fontSize, float /*maxWidth*/) -> Size {
        return {static_cast<float>(text.length()) * pxPerChar, fontSize};
    };
}

}  // namespace

// ---------------------------------------------------------------------------
// Ported coverage: text measurement now flows through an injected ITextMeasurer
// reached via the per-host yoga config context, not a process-global.
// ---------------------------------------------------------------------------

TEST_CASE("Text node uses intrinsic size") {
    FnMeasurer measurer(perChar(10.0f));
    MeasureHarness h;
    h.setMeasurer(&measurer);

    auto tree = Column({
                           Text("Hello").fontSize(20).setKey("txt"),
                       })
                    .width(200)
                    .alignItems(AlignItems::FlexStart);

    auto* root = h.mount(tree);
    root->calculateLayout(200, 100);

    // "Hello" = 5 chars * 10px = 50px width, 20px height
    CHECK(root->children[0]->layout.width == doctest::Approx(50));
    CHECK(root->children[0]->layout.height == doctest::Approx(20));
}

TEST_CASE("Text node respects explicit dimensions") {
    FnMeasurer measurer(perChar(10.0f));
    MeasureHarness h;
    h.setMeasurer(&measurer);

    auto tree = Text("Hello").fontSize(20).width(100).height(40);

    auto* root = h.mount(tree);
    root->calculateLayout(200, 100);

    CHECK(root->layout.width == doctest::Approx(100));
    CHECK(root->layout.height == doctest::Approx(40));
}

TEST_CASE("Text node with flexGrow expands") {
    FnMeasurer measurer(perChar(10.0f));
    MeasureHarness h;
    h.setMeasurer(&measurer);

    auto tree = Row({
                        Text("Hi").fontSize(16).setKey("a"),
                        Text("World").fontSize(16).flexGrow(1).setKey("b"),
                    })
                    .width(200)
                    .height(50);

    auto* root = h.mount(tree);
    root->calculateLayout(200, 50);

    CHECK(root->children[0]->layout.width == doctest::Approx(20));
    CHECK(root->children[1]->layout.width == doctest::Approx(180));  // 200 - 20
}

TEST_CASE("Default fallback measure function (no measurer installed)") {
    MeasureHarness h;
    // No measurer set -> config context is null -> fallbackMeasure is used.

    auto tree = Text("Test").fontSize(12);

    auto* root = h.mount(tree);
    root->calculateLayout(YGUndefined, YGUndefined);

    // Fallback: 0.6 * fontSize per char. "Test" = 4 * (12 * 0.6) = 28.8
    CHECK(root->layout.width == doctest::Approx(28.8f).epsilon(0.1));
    CHECK(root->layout.height == doctest::Approx(12));
}

TEST_CASE("Text updates trigger relayout") {
    FnMeasurer measurer(perChar(10.0f));
    MeasureHarness h;
    h.setMeasurer(&measurer);

    auto tree1 = Text("Hi").fontSize(16).setKey("txt");
    auto* root = h.mount(tree1);
    root->calculateLayout(YGUndefined, YGUndefined);

    CHECK(root->layout.width == doctest::Approx(20));  // "Hi" = 2 * 10

    auto tree2 = Text("Hello World").fontSize(16).setKey("txt");
    h.reconciler().reconcile(h.fiber(), tree2);
    root->calculateLayout(YGUndefined, YGUndefined);

    CHECK(root->layout.width == doctest::Approx(110));  // "Hello World" = 11 * 10
}

// ---------------------------------------------------------------------------
// B7: per-host measurement — two hosts with distinct measurers do not clobber
// each other (impossible under the old process-global Measure singleton).
// ---------------------------------------------------------------------------

TEST_CASE("B7: two hosts with distinct measurers stay independent") {
    FnMeasurer measurerA(perChar(10.0f));  // 10px/char
    FnMeasurer measurerB(perChar(20.0f));  // 20px/char

    MeasureHarness a;
    MeasureHarness b;
    a.setMeasurer(&measurerA);
    b.setMeasurer(&measurerB);

    auto makeTree = [] {
        return Column({Text("Hello").fontSize(16).setKey("txt")})
            .width(400)
            .alignItems(AlignItems::FlexStart);
    };

    auto* rootA = a.mount(makeTree());
    auto* rootB = b.mount(makeTree());

    // Lay both out; under the old global the second mount's measurer would win
    // for both. With per-host configs each uses its own.
    rootA->calculateLayout(400, 100);
    rootB->calculateLayout(400, 100);

    // "Hello" = 5 chars: A -> 50px, B -> 100px, BOTH correct simultaneously.
    CHECK(rootA->children[0]->layout.width == doctest::Approx(50));
    CHECK(rootB->children[0]->layout.width == doctest::Approx(100));
}

TEST_CASE("B7: destroying one host's measurer does not break the other") {
    FnMeasurer measurerB(perChar(20.0f));

    MeasureHarness a;
    MeasureHarness b;
    b.setMeasurer(&measurerB);

    {
        FnMeasurer measurerA(perChar(10.0f));
        a.setMeasurer(&measurerA);

        auto* rootA = a.mount(Column({Text("Hello").fontSize(16).setKey("t")})
                                  .width(400)
                                  .alignItems(AlignItems::FlexStart));
        rootA->calculateLayout(400, 100);
        CHECK(rootA->children[0]->layout.width == doctest::Approx(50));

        // measurerA goes out of scope -> clear A's context so nothing dangles.
        a.setMeasurer(nullptr);
    }

    // B is wholly unaffected by A's teardown.
    auto* rootB = b.mount(Column({Text("Hello").fontSize(16).setKey("t")})
                              .width(400)
                              .alignItems(AlignItems::FlexStart));
    rootB->calculateLayout(400, 100);
    CHECK(rootB->children[0]->layout.width == doctest::Approx(100));
}

// ---------------------------------------------------------------------------
// B6: clearing the measurer is honored — a relayout after setTextMeasurer(null)
// does NOT call the old measurer and falls back. Proves no surviving holder of
// a dangling measurer pointer.
// ---------------------------------------------------------------------------

TEST_CASE("B6: cleared measurer is not called; layout falls back") {
    FnMeasurer counting(perChar(10.0f));
    MeasureHarness h;
    h.setMeasurer(&counting);

    auto tree = Text("Test").fontSize(12).setKey("txt");
    auto* root = h.mount(tree);
    root->calculateLayout(YGUndefined, YGUndefined);

    CHECK(root->layout.width == doctest::Approx(40));  // 4 * 10
    CHECK(counting.calls() > 0);

    // Clear the measurer and force a fresh measurement.
    h.setMeasurer(nullptr);
    int callsBefore = counting.calls();

    auto tree2 = Text("Testing").fontSize(12).setKey("txt");
    h.reconciler().reconcile(h.fiber(), tree2);
    root->calculateLayout(YGUndefined, YGUndefined);

    // The cleared measurer must not be touched again...
    CHECK(counting.calls() == callsBefore);
    // ...and layout falls back: "Testing" = 7 * (12 * 0.6) = 50.4
    CHECK(root->layout.width == doctest::Approx(50.4f).epsilon(0.1));
}

// ---------------------------------------------------------------------------
// Plumbing: a node created via the Host/Reconciler carries a non-default config
// whose context is the injected measurer (the exact channel measureFunc reads).
// ---------------------------------------------------------------------------

TEST_CASE("Config-context plumbing: node config carries the measurer") {
    FnMeasurer measurer(perChar(10.0f));

    // Drive through a real Host to prove production wiring, not just the harness.
    class TestHost : public Host {
    public:
        using Host::Host;
        void install(ITextMeasurer* m) { setTextMeasurer(m); }
    } host;
    host.install(&measurer);

    host.setRender([] { return Text("Hi").fontSize(16); });
    host.update(200, 100);

    Node* root = host.root();
    REQUIRE(root != nullptr);

    YGConfigConstRef cfg = YGNodeGetConfig(root->yogaNode);
    // The node was created against the host's config, not Yoga's default one.
    CHECK(cfg != YGConfigGetDefault());
    // ...and that config's context is the measurer measureFunc will recover.
    CHECK(YGConfigGetContext(cfg) == static_cast<void*>(&measurer));
}

// ---------------------------------------------------------------------------
// Plumbing (deep): EVERY node in a real-Host tree — not merely the root — is
// created against the host's config (so measureFunc can recover the measurer
// at any depth), and none falls back to Yoga's process-default config.
// ---------------------------------------------------------------------------

TEST_CASE("Config-context plumbing: deep tree carries host config at every node") {
    FnMeasurer measurer(perChar(10.0f));

    class TestHost : public Host {
    public:
        using Host::Host;
        void install(ITextMeasurer* m) { setTextMeasurer(m); }
    } host;
    host.install(&measurer);

    // A few levels of nesting with text leaves, so the assertion is meaningful.
    host.setRender([] {
        return Column({
            Row({Text("a").setKey("a"), Text("bb").setKey("bb")}).setKey("r1"),
            Column({
                       Row({Text("ccc").setKey("ccc")}).setKey("r2"),
                       Text("dddd").setKey("d"),
                   })
                .setKey("c2"),
        });
    });
    host.update(400, 300);

    Node* root = host.root();
    REQUIRE(root != nullptr);

    YGConfigConstRef hostCfg = YGNodeGetConfig(root->yogaNode);
    REQUIRE(hostCfg != YGConfigGetDefault());

    int visited = 0;
    std::function<void(Node*)> walk = [&](Node* n) {
        ++visited;
        YGConfigConstRef cfg = YGNodeGetConfig(n->yogaNode);
        CHECK(cfg == hostCfg);
        CHECK(cfg != YGConfigGetDefault());
        for (auto& child : n->children) {
            walk(child.get());
        }
    };
    walk(root);

    // Sanity: the tree was actually deep, not a single node.
    CHECK(visited >= 8);
}

// ---------------------------------------------------------------------------
// B6 (general case): a measurer destroyed while its Host is still alive must
// self-detach — the host's config context goes null and a subsequent relayout
// neither dereferences the dead measurer nor reports its metrics; it falls back
// to the heuristic. This closes the gap that consumer code forgetting to call
// setTextMeasurer(nullptr) before destroying the renderer would otherwise leave.
// ---------------------------------------------------------------------------

TEST_CASE("B6 general: destroying measurer before host clears context and falls back") {
    class TestHost : public Host {
    public:
        using Host::Host;
        void install(ITextMeasurer* m) { setTextMeasurer(m); }
    } host;

    // A text leaf whose intrinsic width Yoga must measure (parent is wider and
    // left-aligns), so the measure callback actually fires.
    host.setRender([] {
        return Column({Text("Test").fontSize(12).setKey("txt")})
            .width(400)
            .alignItems(AlignItems::FlexStart);
    });

    int callsAtDeath = 0;
    {
        FnMeasurer measurer(perChar(10.0f));
        host.install(&measurer);  // canonical one-liner wiring, no extra burden
        host.update(400, 100);

        Node* root = host.root();
        REQUIRE(root != nullptr);
        CHECK(root->children[0]->layout.width == doctest::Approx(40));  // 4 * 10, measurer used
        CHECK(measurer.calls() > 0);

        callsAtDeath = measurer.calls();
        // measurer dies here WITHOUT the consumer calling host.install(nullptr).
        // ~ITextMeasurer must clear the host's config context by itself.
    }

    Node* root = host.root();
    YGConfigConstRef cfg = YGNodeGetConfig(root->yogaNode);
    // Observable proof the dangling pointer was severed: context is now null.
    CHECK(YGConfigGetContext(cfg) == nullptr);

    // Force a fresh measurement. With no ASan, the observable contract is: the
    // dead measurer is not called (severed above) and the fallback width is
    // produced. Reconcile keeps the same nodes, so `root` stays valid.
    host.setRender([] {
        return Column({Text("Testing").fontSize(12).setKey("txt")})
            .width(400)
            .alignItems(AlignItems::FlexStart);
    });
    host.markDirty();
    host.update(400, 100);

    // Fallback: "Testing" = 7 * (12 * 0.6) = 50.4
    CHECK(host.root()->children[0]->layout.width == doctest::Approx(50.4f).epsilon(0.1));
    // Sanity: the measurer had genuinely been driving layout before its death.
    CHECK(callsAtDeath > 0);
}

// ---------------------------------------------------------------------------
// B6 (reverse order): host destroyed before measurer is also safe — the
// measurer's destructor must not touch the freed host. Observable proof: the
// measurer outlives the host and its later destruction does not crash and does
// not report having been invoked post-teardown.
// ---------------------------------------------------------------------------

TEST_CASE("B6 general: destroying host before measurer is safe") {
    FnMeasurer measurer(perChar(10.0f));

    int callsAfterHost = 0;
    {
        class TestHost : public Host {
        public:
            using Host::Host;
            void install(ITextMeasurer* m) { setTextMeasurer(m); }
        } host;
        host.install(&measurer);
        host.setRender([] {
            return Column({Text("Hi").fontSize(16).setKey("t")})
                .width(400)
                .alignItems(AlignItems::FlexStart);
        });
        host.update(400, 100);
        CHECK(measurer.calls() > 0);
        callsAfterHost = measurer.calls();
        // host dies here; ~Host deregisters from `measurer`.
    }

    // measurer is still alive and untouched by the host's teardown.
    CHECK(measurer.calls() == callsAfterHost);
    // Its destructor (at scope exit) must not dereference the freed host —
    // having deregistered, it has no record to act on. Reaching here is the
    // pass; a stale record would have cleared a freed config (UB).
}
