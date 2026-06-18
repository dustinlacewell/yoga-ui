#include <yui/core/ComponentContext.hpp>

#include <yui/core/DirtyScheduler.hpp>

#include <cstdio>
#include <exception>
#include <string>

namespace yui {

// --- ComponentContext ---

ComponentContext::ComponentContext(Fiber* fiber, DirtyScheduler* host) : fiber_(fiber), host_(host), hookIndex_(0) {}

namespace {
const char* kindName(HookTag::Kind k) {
    switch (k) {
        case HookTag::Kind::State:  return "useState";
        case HookTag::Kind::Ref:    return "useRef";
        case HookTag::Kind::Field:  return "useField";
        case HookTag::Kind::Effect: return "useEffect";
    }
    return "?";
}
}  // namespace

// Append-or-check the call-order tag for the hook at `index`. This is the single
// mechanism behind both FIX #7 (any_cast type guard) and FIX #8 (always-on rules-
// of-hooks). On the first render a slot's tag is recorded; on every later render
// the incoming (kind,type) is compared against the recorded tag. A mismatch means
// the Nth hook of this render is not the Nth hook of the first render — a reorder
// or a conditional that swaps hooks at a stable index. Diagnosed through the host
// error sink (never assert/abort: a shipped library must not crash the host DAW).
// Returns true on match (caller may safely any_cast the slot); false on mismatch.
//
// Limitation: two hooks of the SAME kind AND value type that swap positions
// (e.g. two useState<int>) are indistinguishable by tag and slip through — the
// slot already holds an int, so the any_cast is also safe, just cross-wired. Tag
// equality catches every cross-type/cross-kind case, which is strictly better
// than the old count-only check and covers the common bugs.
bool ComponentContext::checkHookTag(size_t index, HookTag::Kind kind, std::type_index type) {
    HookTag incoming{kind, type};
    const char* name = fiber_->debugName ? fiber_->debugName : "<unnamed component>";

    if (index >= fiber_->hookTags.size()) {
        if (!fiber_->hooksEstablished) {
            // First render still building the hook list — record this slot.
            fiber_->hookTags.push_back(incoming);
            return true;
        }
        // A later render reached an index the first render never did: an extra
        // trailing/conditional hook. Diagnose as a count/rules violation.
        std::string msg = "yui: rules-of-hooks violation in component '" + std::string(name) +
                          "': hook count changed between renders (this render called more hooks than "
                          "the first; new " + kindName(incoming.kind) + " at index " +
                          std::to_string(index) + ") — hooks must not be called conditionally or in a loop";
        if (host_) host_->reportError(msg, nullptr);
        return false;
    }

    const HookTag& recorded = fiber_->hookTags[index];
    if (recorded == incoming) return true;

    std::string msg = "yui: rules-of-hooks violation in component '" + std::string(name) +
                      "' at hook index " + std::to_string(index) +
                      ": expected " + kindName(recorded.kind) + "<" + recorded.type.name() + ">" +
                      ", got " + kindName(incoming.kind) + "<" + incoming.type.name() + ">" +
                      " — hook order/type must be identical on every render";
    if (host_) host_->reportError(msg, nullptr);
    return false;
}

void ComponentContext::useEffect(std::function<std::function<void()>()> effect) {
    // useEffect occupies a positional hook slot. Tag it (it carries no value type,
    // so typeid(void)) so an effect that swaps places with a stateful hook is caught
    // by the same rules-of-hooks check. It owns no any slot, so the result is
    // advisory — diagnosed inside checkHookTag, then we proceed regardless.
    checkHookTag(hookIndex_++, HookTag::Kind::Effect, std::type_index(typeid(void)));
    fiber_->pendingEffects.push_back(std::move(effect));
}

// Always-on (release too) count backstop, paired with the per-slot tag check.
// checkHookTag catches reorders/type-changes at any index that IS reached, and the
// "more hooks than the first render" case (an extra trailing hook). This destructor
// catches the complementary case — a render that reached FEWER hooks than the first
// (an early return, a conditional that skips a trailing hook) — which no per-slot
// comparison can see, because the skipped slots are simply never visited. It also
// latches hooksEstablished so checkHookTag can tell first-render growth from a later
// render's extra hook.
ComponentContext::~ComponentContext() {
    // Skip when the component threw mid-render: an exception unwinds through here
    // with a partial hookIndex_, which is not a rules-of-hooks violation. (A throw
    // in flight means the reconciler is already reporting it.) Leave hooksEstablished
    // as-is so the partial list is not frozen as the canonical one.
    if (std::uncaught_exceptions() > 0) return;

    if (!fiber_->hooksEstablished) {
        // First successful render — freeze its hook list as the canonical sequence.
        fiber_->hooksEstablished = true;
        return;
    }

    if (hookIndex_ >= fiber_->hookTags.size()) return;  // steady state (extra hooks already diagnosed)

    const char* name = fiber_->debugName ? fiber_->debugName : "<unnamed component>";
    std::string msg = "yui: rules-of-hooks violation in component '" + std::string(name) +
                      "': hook count changed between renders (expected " +
                      std::to_string(fiber_->hookTags.size()) + ", got " + std::to_string(hookIndex_) +
                      ") — hooks must not be called conditionally or in a loop";
    if (host_) host_->reportError(msg, nullptr);
}

}  // namespace yui
