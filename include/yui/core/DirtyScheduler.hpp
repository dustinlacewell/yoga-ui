#pragma once

#include <exception>
#include <memory>
#include <string_view>

namespace yui {

class Node;

// The narrow seam between core (Reconciler/Fiber/ComponentContext) and the Host.
//
// Core builds and walks the fiber/render trees; the Host owns the dirty-flag
// scheduling, the diagnostic sink, and ownership of the render root. Without this
// interface those concerns are reached through a concrete Host*, which makes core
// depend on the full Host definition — a cycle, since Host owns a Reconciler.
//
// DirtyScheduler is the abstract back-channel core actually uses. Host IS-A
// DirtyScheduler and implements every method; core holds a DirtyScheduler* and
// depends only on this header. Each method here is justified by a real call site
// in core — no speculative surface:
//
//   markComponentDirty()  — Fiber::markDirty (a useState setter / Store::set marks
//                           its owning component fiber dirty; the host re-renders
//                           dirty components at the next update()).
//   markDirty()           — Store::notify (a top-level Store subscription dirties
//                           the whole host for a full re-render).
//   replaceRenderRoot()   — Reconciler::remountRoot (a root-type change rebuilds
//                           the render root, which the host owns after the first
//                           frame, so it must be handed back).
//   reportError()         — the shared diagnostic sink reached from the reconciler,
//                           fiber cleanups/effects, hook checks, and Store. noexcept
//                           because callers route from teardown/unwinding contexts.
//
// All signatures are IDENTICAL to Host's, so Host satisfies the interface by a pure
// extract-interface — no adapter, no behaviour change.
struct DirtyScheduler {
    virtual ~DirtyScheduler() = default;

    virtual void markDirty() = 0;
    virtual void markComponentDirty() = 0;
    virtual void replaceRenderRoot(std::unique_ptr<Node> newRoot) = 0;
    virtual void reportError(std::string_view where, const std::exception* eOrNull) noexcept = 0;
};

}  // namespace yui
