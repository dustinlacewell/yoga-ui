#include "doctest.h"

#include <yui/core/DirtyScheduler.hpp>
#include <yui/core/Fiber.hpp>
#include <yui/core/Reconciler.hpp>

using namespace yui;

namespace {

// A minimal DirtyScheduler with no Host behind it. Proves core (Fiber, Reconciler)
// reaches the host ONLY through the interface — a stub satisfying these four
// methods is enough to drive dirty-marking and reconciliation headless.
struct CountingScheduler : DirtyScheduler {
    int dirty = 0;
    int componentDirty = 0;
    int replacedRoot = 0;
    int errors = 0;

    void markDirty() override { ++dirty; }
    void markComponentDirty() override { ++componentDirty; }
    void replaceRenderRoot(std::unique_ptr<Node>) override { ++replacedRoot; }
    void reportError(std::string_view, const std::exception*) noexcept override { ++errors; }
};

}  // namespace

TEST_CASE("Fiber::markDirty routes through the DirtyScheduler seam") {
    CountingScheduler sched;

    Fiber fiber;
    fiber.tag = Fiber::Tag::Component;
    fiber.host = &sched;  // a DirtyScheduler*, not a Host*

    CHECK_FALSE(fiber.dirty.load());
    fiber.markDirty();

    CHECK(fiber.dirty.load());
    CHECK(sched.componentDirty == 1);  // reached the host via the interface
    CHECK(sched.dirty == 0);           // markDirty() is host-level, not touched here
}

TEST_CASE("Reconciler drives reconciliation against a DirtyScheduler stub") {
    CountingScheduler sched;

    Reconciler reconciler;
    reconciler.setHost(&sched);  // takes a DirtyScheduler*, no concrete Host needed

    auto tree = Box({Text("a").setKey("a")});
    auto fiber = reconciler.mount(tree);

    REQUIRE(fiber != nullptr);
    CHECK(reconciler.renderRoot() != nullptr);

    // A root-type change forces remountRoot, the one Reconciler path that calls
    // back into the scheduler. With a scheduler installed, the freshly-built root
    // is handed off via replaceRenderRoot — proving the reconciler reaches the
    // host purely through the interface (the stub owns the new root; the
    // reconciler's own renderRoot_ is left empty, exactly the with-host branch).
    auto changed = Text("now-text");
    reconciler.reconcile(fiber.get(), changed);

    CHECK(sched.replacedRoot == 1);
    CHECK(reconciler.renderRoot() == nullptr);  // root handed off to the scheduler
    CHECK(sched.errors == 0);
}
