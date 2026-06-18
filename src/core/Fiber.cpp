#include <yui/core/Fiber.hpp>

#include <yui/core/DirtyScheduler.hpp>

#include <exception>

namespace yui {

namespace {

// Run one teardown callback, swallowing any throw. runCleanups/willUnmount fire
// from ~Host / ~Fiber and from the unmount path, so a throwing cleanup must
// neither abort the rest of the loop nor escape a destructor (which would
// terminate). Route to the owning component fiber's host sink when available;
// only component fibers carry cleanups, and they always have host set.
void runOneCleanup(std::function<void()>& cleanup, DirtyScheduler* host) noexcept {
    try {
        cleanup();
    } catch (const std::exception& e) {
        if (host) host->reportError("cleanup", &e);
    } catch (...) {
        if (host) host->reportError("cleanup", nullptr);
    }
}

}  // namespace

void Fiber::runCleanups() noexcept {
    for (auto& cleanup : effectCleanups) {
        runOneCleanup(cleanup, host);
    }
    effectCleanups.clear();

    for (auto& sub : subscriptionCleanups) {
        runOneCleanup(sub.unsubscribe, host);
    }
    subscriptionCleanups.clear();
}

void Fiber::runSubscriptionCleanups() noexcept {
    for (auto& sub : subscriptionCleanups) {
        runOneCleanup(sub.unsubscribe, host);
    }
    subscriptionCleanups.clear();
}

void Fiber::willUnmount() noexcept {
    runCleanups();
    for (auto& child : children) {
        if (child) child->willUnmount();  // defense-in-depth against any transient hole
    }
}

void Fiber::markDirty() {
    dirty.store(true, std::memory_order_relaxed);
    if (host) {
        host->markComponentDirty();
    }
}

void Fiber::runPendingEffects() {
    // Each effect body is a user callback; isolate a throw so one bad effect does
    // not skip the rest. A throwing effect registers no cleanup. The whole list is
    // still drained either way.
    for (auto& effect : pendingEffects) {
        try {
            auto cleanup = effect();
            if (cleanup) {
                effectCleanups.push_back(std::move(cleanup));
            }
        } catch (const std::exception& e) {
            if (host) host->reportError("effect", &e);
        } catch (...) {
            if (host) host->reportError("effect", nullptr);
        }
    }
    pendingEffects.clear();
}

}  // namespace yui
