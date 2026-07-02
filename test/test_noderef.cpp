#include <yui/yui.hpp>

#include "doctest.h"

#include <functional>
#include <string>

using namespace yui;

namespace {
class TestHost : public Host {
public:
    TestHost() = default;
};
}  // namespace

// ─── absoluteRect: accumulation through nesting and scroll ───────────────────

TEST_CASE("absoluteRect: nested boxes accumulate parent offsets") {
    TestHost host;
    NodeRef inner;

    host.setRender(std::function<VNode()>([&]() {
        // Outer padded box → inner box offset by the padding within it.
        return Box(
            Box().ref(inner).width(40).height(20)
        ).padding(15).width(200).height(100);
    }));
    host.update(300, 300);

    Node* n = inner.current();
    REQUIRE(n != nullptr);
    auto r = absoluteRect(n);
    // Inner box sits at the outer box's content origin = padding (15,15).
    CHECK(r.x == doctest::Approx(15));
    CHECK(r.y == doctest::Approx(15));
    CHECK(r.w == doctest::Approx(40));
    CHECK(r.h == doctest::Approx(20));
}

TEST_CASE("absoluteRect: a scrolled ancestor subtracts its scroll offset") {
    TestHost host;
    NodeRef row;
    ScrollNode* scrollNode = nullptr;

    host.setRender(std::function<VNode()>([&]() {
        return Box(
            Scroll(Column(
                Box().height(50),
                Box().ref(row).height(50),   // 2nd row → content y = 50
                Box().height(50)
            )).height(80)                   // viewport shorter than content
        );
    }));
    host.update(200, 200);

    // Find the ScrollNode in the render tree and scroll it.
    std::function<void(Node*)> find = [&](Node* p) {
        if (p->type() == PrimitiveType::Scroll) scrollNode = static_cast<ScrollNode*>(p);
        for (auto& c : p->children) find(c.get());
    };
    find(host.root());
    REQUIRE(scrollNode != nullptr);

    Node* n = row.current();
    REQUIRE(n != nullptr);
    float yBefore = absoluteRect(n).y;

    // Scroll the content up by 30; the row's drawn Y must drop by 30.
    scrollNode->scrollOffsetY = 30;
    float yAfter = absoluteRect(n).y;
    CHECK(yAfter == doctest::Approx(yBefore - 30));
}

TEST_CASE("absoluteRect: a padded scroll ancestor offsets children by the viewport origin") {
    TestHost host;
    NodeRef row;
    ScrollNode* scrollNode = nullptr;

    host.setRender(std::function<VNode()>([&]() {
        return Box(
            Scroll(Column(
                Box().height(50),
                Box().ref(row).height(50)    // 2nd row → content y = 50
            )).height(80).padding(10)
        );
    }));
    host.update(200, 200);

    std::function<void(Node*)> find = [&](Node* p) {
        if (p->type() == PrimitiveType::Scroll) scrollNode = static_cast<ScrollNode*>(p);
        for (auto& c : p->children) find(c.get());
    };
    find(host.root());
    REQUIRE(scrollNode != nullptr);

    // Content draws at the PADDED viewport origin: the row's drawn rect is
    // inset by the scroll's padding on both axes (then shifted by the offset).
    auto r = row.getBoundingRect();
    CHECK(r.x == doctest::Approx(10));
    CHECK(r.y == doctest::Approx(10 + 50));

    scrollNode->scrollOffsetY = 30;
    CHECK(row.getBoundingRect().y == doctest::Approx(10 + 50 - 30));
}

// ─── asScroll: the sanctioned route to the programmatic scroll API ───────────

TEST_CASE("NodeRef::asScroll downcasts scroll elements; others read null") {
    TestHost host;
    NodeRef scrollRef;
    NodeRef boxRef;

    host.setRender(std::function<VNode()>([&]() {
        return Box(
            Scroll(Column(
                Box().height(50), Box().height(50), Box().height(50)
            )).ref(scrollRef).height(80),
            Box().ref(boxRef).height(10)
        );
    }));
    host.update(200, 200);

    CHECK(boxRef.asScroll() == nullptr);
    ScrollNode* s = scrollRef.asScroll();
    REQUIRE(s != nullptr);

    // The handle drives the real API, through the single clamp:
    // maxScroll = 150 - 80.
    s->scrollTo(0, 20);
    CHECK(s->targetScrollY == doctest::Approx(20));
    s->scrollTo(0, 1000);
    CHECK(s->targetScrollY == doctest::Approx(70));
}

// ─── Render-phase guard: current() is null during render (React parity) ──────

TEST_CASE("NodeRef::current returns null during render, valid after") {
    TestHost host;
    NodeRef ref;
    bool nullDuringRender = false;

    auto Comp = [&](ComponentContext& ctx) -> VNode {
        auto r = ctx.useElementRef();
        ref = r;
        // Reading the ref in the component body (render phase) must be null —
        // matching React (refs are null during render).
        nullDuringRender = (r.current() == nullptr);
        return Box().ref(r).width(30).height(30);
    };

    host.setRender(std::function<VNode()>([&]() { return Box(Component(Comp)); }));
    host.update(200, 200);

    CHECK(nullDuringRender);             // null while rendering
    CHECK(ref.current() != nullptr);     // valid once render/layout settled
}

// ─── Liveness: ref nulls when the element is reconciled away ──────────────────

TEST_CASE("NodeRef::current nulls out after the element unmounts") {
    TestHost host;
    NodeRef ref;
    bool show = true;

    // Hold the ref outside the render so it survives the element's removal.
    // The ref'd Box is the SECOND child; toggling `show` removes it entirely,
    // forcing an unmount (not a positional reuse).
    host.setRender(std::function<VNode()>([&]() -> VNode {
        std::vector<Child> kids;
        kids.push_back(Box().width(10).height(10));
        if (show) kids.push_back(Box().ref(ref).width(30).height(30));
        return Box(std::move(kids));
    }));

    host.update(200, 200);
    REQUIRE(ref.current() != nullptr);   // mounted → live

    show = false;
    host.markDirty();                    // force the root render closure to re-run
    host.update(200, 200);               // reconcile away the ref'd node
    CHECK(ref.current() == nullptr);     // node destroyed → ref reads null, no UAF
}

// ─── Handle stability: same slot across re-renders ───────────────────────────

TEST_CASE("useElementRef returns a stable handle across re-renders") {
    TestHost host;
    const NodeRefSlot* slotFirst = nullptr;
    const NodeRefSlot* slotLater = nullptr;
    int renders = 0;
    std::function<void(int)> bump;

    auto Comp = [&](ComponentContext& ctx) -> VNode {
        auto r = ctx.useElementRef();
        auto [n, setN] = ctx.useState<int>(0);
        bump = setN;                     // capture the setter to force a re-render
        // The underlying slot identity must be the same object every render.
        if (renders == 0) slotFirst = r.slot().get();
        else slotLater = r.slot().get();
        renders++;
        return Box().ref(r).width(30).height(30);
    };

    host.setRender(std::function<VNode()>([&]() { return Box(Component(Comp)); }));
    host.update(200, 200);
    bump(1);                             // state change → re-render of the SAME fiber
    host.update(200, 200);

    REQUIRE(renders >= 2);
    REQUIRE(slotFirst != nullptr);
    REQUIRE(slotLater != nullptr);
    CHECK(slotFirst == slotLater);       // same slot → captured ref stays valid
}

// ─── Ref resolves to the attached node, not a child ──────────────────────────

TEST_CASE("ref attaches to the element it's set on, not a descendant") {
    TestHost host;
    NodeRef rowRef;

    host.setRender(std::function<VNode()>([&]() {
        // A row with text children; the ref is on the ROW.
        return Box(
            Row( Text("label"), Text("x") ).ref(rowRef).width(120).height(26)
        ).padding(5);
    }));
    host.update(300, 300);

    Node* n = rowRef.current();
    REQUIRE(n != nullptr);
    CHECK(n->type() == PrimitiveType::Box);      // Row is a Box, not Text
    auto r = absoluteRect(n);
    CHECK(r.w == doctest::Approx(120));          // the row's width, not a text cell's
    CHECK(r.h == doctest::Approx(26));
}
