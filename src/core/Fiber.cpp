#include <yui/core/Fiber.hpp>

#include <yui/core/Host.hpp>

namespace yui {

void Fiber::runCleanups() {
    for (auto& cleanup : effectCleanups) {
        cleanup();
    }
    effectCleanups.clear();

    for (auto& cleanup : subscriptionCleanups) {
        cleanup();
    }
    subscriptionCleanups.clear();
}

void Fiber::willUnmount() {
    runCleanups();
    for (auto& child : children) {
        child->willUnmount();
    }
}

void Fiber::markDirty() {
    dirty = true;
    if (host) {
        host->markComponentDirty();
    }
}

void Fiber::runPendingEffects() {
    for (auto& effect : pendingEffects) {
        auto cleanup = effect();
        if (cleanup) {
            effectCleanups.push_back(std::move(cleanup));
        }
    }
    pendingEffects.clear();
}

}  // namespace yui
