#include <yui/core/ComponentContext.hpp>

#include <cassert>
#include <cstdio>

namespace yui {

// --- ComponentContext ---

ComponentContext::ComponentContext(Fiber* fiber, Host* host) : fiber_(fiber), host_(host), hookIndex_(0) {}

ComponentContext::~ComponentContext() {
#ifndef NDEBUG
    // Validate rules of hooks - hook call order must be consistent across renders
    if (fiber_->expectedHookCount == 0) {
        // First render - record the hook count
        fiber_->expectedHookCount = hookIndex_;
    } else if (fiber_->expectedHookCount != hookIndex_) {
        // Hook count changed - this is a bug!
        const char* name = fiber_->debugName ? fiber_->debugName : "<unnamed component>";
        fprintf(stderr,
                "ERROR: Hook count mismatch in component '%s'\n"
                "  Expected: %zu hooks\n"
                "  Got:      %zu hooks\n"
                "  This usually means you're calling hooks conditionally or in a loop.\n"
                "  Hooks must be called in the same order on every render.\n",
                name, fiber_->expectedHookCount, hookIndex_);
        assert(false && "Hook count mismatch - rules of hooks violated");
    }
#endif
}

void ComponentContext::useEffect(std::function<std::function<void()>()> effect) {
    hookIndex_++;  // useEffect counts as a hook call
    fiber_->pendingEffects.push_back(std::move(effect));
}

}  // namespace yui
