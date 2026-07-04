#include <yui/detail/Reconciler.hpp>

#include <yui/core/ComponentContext.hpp>
#include <yui/core/DirtyScheduler.hpp>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace yui {

// --- Helpers for Child variant ---

// Bind a VNode's element ref (if it set one via BuilderBase::ref) to the render
// node created/reused for it. Captures the node's liveness token in lockstep so
// the ref nulls out when the node is later destroyed (mirrors setFocusedInput).
// Called at every site where a render Node is bound to its VNode, so the ref
// self-heals across remounts (the bind re-runs each render the element appears).
static void bindRefSlot(Node* node, const VNode& vnode) {
    if (!vnode.ref || !node) return;
    auto& slot = *vnode.ref->slot();
    slot.node = node;
    slot.alive = node->alive;
}

static bool isChildEmpty(const Child& child) {
    return std::visit(
        [](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, VNode>) {
                return c.isEmpty;
            } else {
                return false;
            }
        },
        child);
}

static bool isComponent(const Child& child) {
    return std::holds_alternative<Component>(child);
}

static bool hasIntKey(const Child& child) {
    return std::visit(
        [](const auto& c) { return c.hasIntKey(); }, child);
}

static int64_t getIntKey(const Child& child) {
    return std::visit(
        [](const auto& c) { return c.intKey; }, child);
}

static bool hasStringKey(const Child& child) {
    return std::visit(
        [](const auto& c) { return c.hasStringKey(); }, child);
}

static const std::string& getStringKey(const Child& child) {
    // Return a reference into the Child (which outlives the call); callers only
    // read it, so no copy is made.
    return std::visit(
        [](const auto& c) -> const std::string& { return c.key; }, child);
}

// Component identity: (target_type, target function-pointer address). The
// closure type of a lambda-expression is unique per expression and stable across
// renders, so target_type() distinguishes different component lambdas at the same
// slot. Plain function pointers all share one closure-less target_type, so also
// capture the pointed-to function address to disambiguate &A vs &B.
struct CompId {
    std::type_index type;
    const void* ptr;
};

static CompId componentIdOf(const ComponentFn& fn) {
    std::type_index t = std::type_index(fn.target_type());
    const void* p = nullptr;
    if (auto pp = fn.target<VNode (*)(ComponentContext&)>()) {
        p = reinterpret_cast<const void*>(*pp);
    }
    return {t, p};
}

// --- Fiber child lookup (mirrors old ChildLookup but for fibers) ---

struct FiberLookup {
    std::unordered_map<int64_t, size_t> byIntKey;
    std::unordered_map<std::string, size_t> byStringKey;
    std::unordered_map<size_t, size_t> byPosition;

    void build(const std::vector<std::unique_ptr<Fiber>>& fibers) {
        for (size_t i = 0; i < fibers.size(); ++i) {
            const auto& f = fibers[i];
            if (f->hasIntKey()) {
                byIntKey[f->intKey] = i;
            } else if (f->hasStringKey()) {
                byStringKey[f->key] = i;
            } else if (f->sourcePosition != Fiber::NO_SOURCE_POSITION) {
                byPosition[f->sourcePosition] = i;
            }
        }
    }

    // Check if a Child matches a Fiber (type compatibility)
    static bool typeMatches(const Child& child, const Fiber& fiber) {
        if (isComponent(child)) {
            if (!fiber.isComponent()) return false;
            // Same slot, DIFFERENT component identity => not a match; the caller's
            // no-match branch remounts (fresh fiber, fresh hook state) rather than
            // letting the new component inherit the old one's state.
            CompId id = componentIdOf(std::get<Component>(child).fn);
            return id.type == fiber.componentType && id.ptr == fiber.componentTargetPtr;
        } else {
            if (!fiber.isHost() || !fiber.renderNode) return false;
            return fiber.renderNode->type() == std::get<VNode>(child).type();
        }
    }

    size_t find(const Child& child, size_t childIndex,
                const std::vector<std::unique_ptr<Fiber>>& fibers,
                const std::vector<bool>& reused) const {
        // Try int key first
        if (hasIntKey(child)) {
            auto it = byIntKey.find(getIntKey(child));
            if (it != byIntKey.end() && !reused[it->second] &&
                typeMatches(child, *fibers[it->second])) {
                return it->second;
            }
        }
        // Fall back to string key
        else if (hasStringKey(child)) {
            auto it = byStringKey.find(getStringKey(child));
            if (it != byStringKey.end() && !reused[it->second] &&
                typeMatches(child, *fibers[it->second])) {
                return it->second;
            }
        }
        // Fall back to position match
        else {
            auto it = byPosition.find(childIndex);
            if (it != byPosition.end() && !reused[it->second] &&
                typeMatches(child, *fibers[it->second])) {
                return it->second;
            }
        }
        return SIZE_MAX;
    }
};

// ============================================================================
// Mounting
// ============================================================================

std::unique_ptr<Fiber> Reconciler::mount(VNode&& vnode) {
    std::unique_ptr<Fiber> fiber;
    try {
        fiber = mountImpl(std::move(vnode));
    } catch (...) {
        discardCommit();  // half-built tree: drop the queue, never fault a later drain
        throw;
    }
    drainCommit();
    return fiber;
}

std::unique_ptr<Fiber> Reconciler::mountImpl(VNode&& vnode) {
    if (vnode.isEmpty) {
        return nullptr;
    }

    // The root is always a host node. Mount it without a render parent
    // (the render root is stored in renderRoot_).
    auto rootNode = createNode(vnode.type(), config_);
    // Peek identity/ref (const reads) BEFORE moving props into the node.
    rootNode->key = vnode.key;
    rootNode->intKey = vnode.intKey;
    bindRefSlot(rootNode.get(), vnode);
    rootNode->updateProps(std::move(vnode.props));

    auto fiber = std::make_unique<Fiber>();
    fiber->tag = Fiber::Tag::Host;
    fiber->key = vnode.key;
    fiber->intKey = vnode.intKey;
    fiber->sourcePosition = 0;
    fiber->renderNode = rootNode.get();

    renderRoot_ = std::move(rootNode);

    // Mount children into the root render node
    size_t renderIndex = 0;
    for (size_t i = 0; i < vnode.children.size(); ++i) {
        auto& child = vnode.children[i];
        if (isChildEmpty(child)) continue;

        auto childFiber = mountChild(std::move(child), i, fiber->renderNode, renderIndex);
        if (childFiber) {
            childFiber->parent = fiber.get();
            fiber->children.push_back(std::move(childFiber));
        }
    }

    return fiber;
}

std::unique_ptr<Fiber> Reconciler::mountHost(VNode&& vnode, size_t sourcePos,
                                              Node* renderParent, size_t& renderIndex) {
    if (vnode.isEmpty) return nullptr;

    auto fiber = std::make_unique<Fiber>();
    fiber->tag = Fiber::Tag::Host;
    fiber->key = vnode.key;
    fiber->intKey = vnode.intKey;
    fiber->sourcePosition = sourcePos;

    // Create render node
    auto node = createNode(vnode.type(), config_);
    node->key = vnode.key;
    node->intKey = vnode.intKey;
    bindRefSlot(node.get(), vnode);

    // Peek autoFocus (a const read of the shared EventProps slice) BEFORE moving
    // props into the node. Any primitive may request mount focus.
    bool wantsAutoFocus = std::visit(
        [](const auto& p) { return static_cast<const EventProps&>(p).autoFocus.value_or(false); },
        vnode.props);

    // Peek the portal focus trap alongside. Fired below BEFORE the children
    // mount: the trap must be installed (and the previously-focused node saved)
    // before any content autoFocus runs, or the save would capture the portal's
    // own freshly-focused content instead of the pre-portal focus. That
    // ordering requirement is why this is a mount peek and not a mount effect —
    // effects run after the whole pass, after every content autoFocus.
    bool wantsTrap = false;
    if (const auto* portalProps = std::get_if<PortalProps>(&vnode.props))
        wantsTrap = portalProps->trapFocus.value_or(false);

    node->updateProps(std::move(vnode.props));
    fiber->renderNode = node.get();

    // Insert into render tree
    insertRenderNode(renderParent, std::move(node), renderIndex);
    renderIndex++;

    // Notify host of a portal focus trap FIRST — the trap's focus save must
    // precede ANY mount focus change (this node's own autoFocus included).
    if (wantsTrap && onTrapMounted_) {
        onTrapMounted_(fiber->renderNode);
    }

    // Notify host of autoFocus request
    if (wantsAutoFocus && onAutoFocus_) {
        onAutoFocus_(fiber->renderNode);
    }

    // Mount children — this fiber's renderNode is the render parent for children
    size_t childRenderIndex = 0;
    for (size_t i = 0; i < vnode.children.size(); ++i) {
        auto& child = vnode.children[i];
        if (isChildEmpty(child)) continue;

        auto childFiber = mountChild(std::move(child), i, fiber->renderNode, childRenderIndex);
        if (childFiber) {
            childFiber->parent = fiber.get();
            fiber->children.push_back(std::move(childFiber));
        }
    }

    return fiber;
}

std::unique_ptr<Fiber> Reconciler::mountComponent(Component&& comp, size_t sourcePos,
                                                    Node* renderParent, size_t& renderIndex) {
    auto fiber = std::make_unique<Fiber>();
    fiber->tag = Fiber::Tag::Component;
    fiber->key = comp.key;
    fiber->intKey = comp.intKey;
    fiber->sourcePosition = sourcePos;
    // Stamp identity and copy debugName from comp BEFORE moving comp.fn (identity
    // is derived from the function target, which the move would empty).
    {
        CompId id = componentIdOf(comp.fn);
        fiber->componentType = id.type;
        fiber->componentTargetPtr = id.ptr;
    }
    fiber->debugName = comp.debugName;  // unconditional: feeds always-on hook diagnostics
    fiber->componentFn = std::move(comp.fn);
    fiber->host = host_;

    // Call the component function. If it throws, discard this never-published
    // fiber (its subscriptions self-unsubscribe via ~Fiber's cleanup path) and
    // mount nothing — no live fiber exists to corrupt. Returning nullptr matches
    // the empty-result contract: the caller simply attaches no child.
    VNode result;
    {
        FiberRenderContext fiberCtx(fiber.get());
        ComponentContext ctx(fiber.get(), host_);
        try {
            result = fiber->componentFn(ctx);
        } catch (const std::exception& e) {
            fiber->runCleanups();  // unsubscribe anything the partial render registered
            reportError("mountComponent", &e);
            return nullptr;
        } catch (...) {
            fiber->runCleanups();
            reportError("mountComponent", nullptr);
            return nullptr;
        }
    }

    // Mount the result — render parent is the SAME as ours (component is invisible)
    if (!result.isEmpty) {
        auto childFiber = mountHost(std::move(result), 0, renderParent, renderIndex);
        if (childFiber) {
            childFiber->parent = fiber.get();
            fiber->children.push_back(std::move(childFiber));
        }
    }

    // Defer effects to the commit phase, AFTER children mount successfully. If a
    // child mount throws (bad_alloc / Yoga / createNode / …), this fiber is
    // destroyed as the exception unwinds, so its raw pointer must never have been
    // enqueued — pushing here (the success path only) guarantees that. mountHost
    // above ran during the pass and bound this fiber's element refs; the deferred
    // effect drains after the whole pass, so a mount effect reading a useElementRef
    // sees it already bound (defect C, mount side). The fiber stays owned by the
    // live tree, so a raw pointer is safe in the effects queue. Order is preserved:
    // a component's effect drains after ALL of the pass's mounts regardless.
    commit_.effects.push_back(fiber.get());

    return fiber;
}

std::unique_ptr<Fiber> Reconciler::mountChild(Child&& child, size_t sourcePos,
                                                Node* renderParent, size_t& renderIndex) {
    if (isChildEmpty(child)) return nullptr;

    if (std::holds_alternative<VNode>(child)) {
        return mountHost(std::move(std::get<VNode>(child)), sourcePos, renderParent, renderIndex);
    } else {
        return mountComponent(std::move(std::get<Component>(child)), sourcePos, renderParent, renderIndex);
    }
}

// ============================================================================
// Reconciliation
// ============================================================================

void Reconciler::reconcile(Fiber* fiber, VNode&& vnode) {
    try {
        reconcileImpl(fiber, std::move(vnode));
    } catch (...) {
        discardCommit();  // half-built tree: drop the queue, never fault a later drain
        throw;
    }
    drainCommit();
}

void Reconciler::reconcileImpl(Fiber* fiber, VNode&& vnode) {
    if (!fiber || vnode.isEmpty) return;

    // Must be a host fiber
    if (!fiber->isHost() || !fiber->renderNode) return;

    // Root primitive type changed between frames (the public render API allows
    // returning a different root each frame). updateProps would std::get<> the
    // wrong props variant and throw, so remount instead — mirroring the child
    // path / rerenderComponent, which remount on type mismatch.
    if (fiber->renderNode->type() != vnode.type()) {
        remountRoot(fiber, std::move(vnode));
        return;
    }

    // Bind the ref (peeks vnode.ref) BEFORE the props/children are moved out.
    bindRefSlot(fiber->renderNode, vnode);
    // Update props on the render node (moves props out of the VNode)
    fiber->renderNode->updateProps(std::move(vnode.props));

    // Reconcile children
    reconcileChildren(fiber, std::move(vnode.children), fiber->renderNode);
}

void Reconciler::remountRoot(Fiber* fiber, VNode&& vnode) {
    // Transactional: build the NEW tree fully before destroying the old one. If
    // mount() fails for any reason (a callback throwing, bad_alloc, …) we return
    // with the old root still fully intact — no dangling renderRoot_, no
    // use-after-free on the next frame.
    //
    // Park the old render root in a local first. Without a host the reconciler
    // still owns renderRoot_ (the host takes it after the first frame); moving it
    // aside both keeps it alive for an orderly teardown below and frees the
    // renderRoot_ slot so mount() can populate it without clobbering live nodes.
    auto oldRenderRoot = std::move(renderRoot_);

    // If mount() throws (a callback, bad_alloc, …), restore the old render root so
    // the tree stays intact and the next frame does not fault on a null renderRoot_.
    std::unique_ptr<Fiber> fresh;
    try {
        fresh = mount(std::move(vnode));  // populates renderRoot_ with the new tree
    } catch (...) {
        renderRoot_ = std::move(oldRenderRoot);
        throw;
    }
    auto freshRenderRoot = takeRenderRoot();

    // The new tree is built and safely parked in freshRenderRoot. Only now tear
    // down the old root subtree's lifecycle (effects/subscriptions) and notify the
    // host that its render nodes are gone. The root render node has no render
    // parent, so it is dropped via ownership (oldRenderRoot) rather than
    // removeRenderNode.
    notifyRenderRemoved(fiber->renderNode);
    fiber->willUnmount();
    oldRenderRoot.reset();

    // Falsify the OLD root fiber's liveness token BEFORE the move overwrites it with
    // fresh's token. Consumers (useState setters, refs) captured the old token; the
    // move would otherwise drop it without flipping it false, leaving those callbacks
    // believing the (now-replaced) fiber is still live.
    if (fiber->alive) *fiber->alive = false;

    // Move the fresh fiber's state into the existing root fiber so the host's
    // fiberRoot_ pointer (and any parent links into it) stay valid.
    *fiber = std::move(*fresh);
    for (auto& child : fiber->children) {
        child->parent = fiber;
    }

    // Install the newly-built render root. After the first frame the host owns the
    // render root (via takeRenderRoot()), so it must be handed back through
    // replaceRenderRoot(); before that — and in host-less reconciler tests — the
    // reconciler keeps ownership in renderRoot_.
    if (host_) {
        host_->replaceRenderRoot(std::move(freshRenderRoot));
    } else {
        renderRoot_ = std::move(freshRenderRoot);
    }
}

void Reconciler::reconcileHost(Fiber* fiber, VNode&& vnode) {
    if (!fiber || !fiber->isHost() || !fiber->renderNode) return;

    // Update identity + bind ref (peeks) BEFORE moving props/children out.
    fiber->key = vnode.key;
    fiber->intKey = vnode.intKey;
    bindRefSlot(fiber->renderNode, vnode);

    // Update props (moves props out of the VNode)
    fiber->renderNode->updateProps(std::move(vnode.props));

    // Reconcile children
    reconcileChildren(fiber, std::move(vnode.children), fiber->renderNode);
}

void Reconciler::reconcileComponent(Fiber* fiber, Component&& comp) {
    if (!fiber || !fiber->isComponent()) return;
    // Stamp identity and copy debugName BEFORE moving comp.fn (identity derives
    // from the function target, which the move empties).
    {
        CompId id = componentIdOf(comp.fn);
        fiber->componentType = id.type;
        fiber->componentTargetPtr = id.ptr;
    }
    fiber->debugName = comp.debugName;
    fiber->componentFn = std::move(comp.fn);
    rerenderComponent(fiber);
}

void Reconciler::reconcileChildren(Fiber* parentFiber, std::vector<Child>&& children,
                                    Node* renderParent) {
    // Build lookup from existing fiber children
    FiberLookup lookup;
    lookup.build(parentFiber->children);

    std::vector<bool> reused(parentFiber->children.size(), false);
    bool childrenUnchanged = true;
    size_t newChildIndex = 0;

    // Plan the new ordering WITHOUT moving ownership out of the live vector. Each
    // slot is either a reused existing child (referenced by index, still owned in
    // place) or a freshly-mounted fiber (owned here until the final swap). The
    // live parentFiber->children keeps all of its entries — no transient null
    // holes — so any observable iteration (willUnmount, ~Host) stays sound even if
    // a mount/reconcile below were to bail out early.
    struct PlannedSlot {
        size_t reuseIndex = SIZE_MAX;          // index into parentFiber->children, or SIZE_MAX
        std::unique_ptr<Fiber> freshFiber;     // owns a newly-mounted fiber otherwise
    };
    std::vector<PlannedSlot> plan;

    for (size_t i = 0; i < children.size(); ++i) {
        // `child` is now a mutable reference — but every PEEK below (isChildEmpty,
        // lookup.find, hasIntKey/getIntKey, hasStringKey/getStringKey) runs BEFORE
        // any move, so nothing reads a moved-from Child.
        auto& child = children[i];
        if (isChildEmpty(child)) continue;

        size_t existingIndex = lookup.find(child, i, parentFiber->children, reused);

        if (existingIndex != SIZE_MAX) {
            // Reuse existing fiber — mutate through the raw pointer, leaving the
            // unique_ptr owned in place in parentFiber->children.
            reused[existingIndex] = true;
            auto& existingFiber = parentFiber->children[existingIndex];
            existingFiber->sourcePosition = i;

            // Update keys (peeks, before the move)
            if (hasIntKey(child)) existingFiber->intKey = getIntKey(child);
            if (hasStringKey(child)) existingFiber->key = getStringKey(child);

            // Reconcile based on type — move the payload out of the Child.
            if (std::holds_alternative<VNode>(child)) {
                reconcileHost(existingFiber.get(), std::move(std::get<VNode>(child)));
            } else {
                reconcileComponent(existingFiber.get(), std::move(std::get<Component>(child)));
            }

            plan.push_back(PlannedSlot{existingIndex, nullptr});

            if (existingIndex != newChildIndex) {
                childrenUnchanged = false;
            }
        } else {
            // Create new fiber
            childrenUnchanged = false;

            // Use end-of-list renderIndex — we'll rebuild render children after
            size_t dummyRenderIndex = renderParent->children.size();
            auto newFiber = mountChild(std::move(child), i, renderParent, dummyRenderIndex);
            if (newFiber) {
                newFiber->parent = parentFiber;
                plan.push_back(PlannedSlot{SIZE_MAX, std::move(newFiber)});
            }
        }
        newChildIndex++;
    }

    if (plan.size() != parentFiber->children.size()) {
        childrenUnchanged = false;
    }

    // Unmount fibers that weren't reused. unmountFiber removes render nodes INLINE
    // (needed during the pass for correct layout) and RECORDS child-first cleanup
    // order into the commit queue, but does NOT run cleanups inline. The swap below
    // would drop these unique_ptrs and destroy the subtrees, dangling the raw
    // pointers the commit queue holds — so transfer OWNERSHIP into commit_.removedRoots
    // here, keeping the subtrees alive until drainCommit runs their deferred cleanups.
    for (size_t i = 0; i < parentFiber->children.size(); ++i) {
        if (!reused[i] && parentFiber->children[i]) {
            unmountFiber(parentFiber->children[i].get(), renderParent);
            commit_.removedRoots.push_back(std::move(parentFiber->children[i]));
        }
    }

    // Materialize the new ordering in a single pass and swap it in atomically.
    // Moving out of the live vector happens only here, immediately before the
    // assignment, so no observable null-hole window ever exists.
    std::vector<std::unique_ptr<Fiber>> newFibers;
    newFibers.reserve(plan.size());
    for (auto& slot : plan) {
        if (slot.reuseIndex != SIZE_MAX) {
            newFibers.push_back(std::move(parentFiber->children[slot.reuseIndex]));
        } else {
            newFibers.push_back(std::move(slot.freshFiber));
        }
    }

    parentFiber->children = std::move(newFibers);

    // Rebuild render parent's children in correct order if anything changed
    if (!childrenUnchanged) {
        rebuildRenderChildren(parentFiber, renderParent);
    }
}

// ============================================================================
// Dirty component re-render
// ============================================================================

bool Reconciler::reconcileDirtyComponents(Fiber* fiber) {
    bool anyReconciled;
    try {
        anyReconciled = reconcileDirtyComponentsImpl(fiber);
    } catch (...) {
        discardCommit();  // half-built tree: drop the queue, never fault a later drain
        throw;
    }
    drainCommit();
    return anyReconciled;
}

bool Reconciler::reconcileDirtyComponentsImpl(Fiber* fiber) {
    if (!fiber) return false;

    bool anyReconciled = false;

    if (fiber->isComponent() && fiber->dirty.load(std::memory_order_relaxed)) {
        // Re-render this component (which reconciles its entire subtree),
        // so we don't walk its children — they're handled by rerenderComponent.
        rerenderComponent(fiber);
        anyReconciled = true;
    } else {
        for (auto& child : fiber->children) {
            // Recurse via the IMPL so drain happens once, at the top-level wrapper.
            if (reconcileDirtyComponentsImpl(child.get())) {
                anyReconciled = true;
            }
        }
    }

    return anyReconciled;
}

void Reconciler::rerenderComponent(Fiber* fiber) {
    if (!fiber || !fiber->isComponent()) return;

    // Transactional render — don't-clear-until-success. The fiber's subscriptions
    // are NEUTRAL across a failed render and exactly the current render's set after
    // a successful one. Neither the tree nor the subscription state is destructively
    // mutated before the render is known to have succeeded.
    //
    // Why this ordering (vs. the obvious "run old cleanups, then render"):
    //   A Store's fiberSubscribers_ set is keyed by Fiber*. The Store::set() that
    //   marked this fiber dirty already REMOVED it from that store's set (notify()
    //   clears subscribers). So at entry the triggering store has no membership to
    //   fall back on. If we ran the old cleanups up front and the render then threw
    //   BEFORE its use() (legal: hooks run in body order), the fiber would be left
    //   unsubscribed from every store with no use() to re-arm it — permanently dead
    //   to those stores. That is the bug this path fixes.
    //
    // Snapshot the OLD subscription records out (leaving the fiber's list empty so
    // the render's use() calls append into a clean vector) WITHOUT running them, so
    // the fiber keeps whatever memberships it still had at entry. Then render. The
    // old records carry both an unsubscribe and a resubscribe action plus the store
    // identity, which is what makes both exits exact:
    //
    //   ON THROW  — leave subscriptions EXACTLY as they were at entry. Undo only the
    //     genuinely-new memberships this partial render added (run each fresh
    //     record's unsubscribe), then re-arm the pre-render memberships (run each
    //     old record's resubscribe — necessary because set() had consumed the
    //     triggering store's membership), then restore the old record list. Net: the
    //     fiber is subscribed to exactly its pre-render stores, one record each,
    //     dirty still set, children untouched — so a later set() still re-renders it.
    //
    //   ON SUCCESS — keep exactly the current render's subscriptions. The fresh
    //     records already hold every store the render use()'d (membership re-inserted
    //     by use()). Dedupe the old records by store identity: an old record whose
    //     store the render re-used is redundant (fresh record + live membership
    //     supersede it) and is dropped WITHOUT running — running its unsubscribe
    //     would erase the still-wanted membership. An old record whose store the
    //     render no longer uses is torn down (run its unsubscribe). End state: the
    //     fiber is subscribed to exactly the stores its current render use()'d, once
    //     each, with one record each.
    //
    // Effect cleanups persist across re-renders (they fire on unmount), so only the
    // subscription records participate here. Each unsubscribe/resubscribe is liveness
    // -guarded and isolated, so a throwing teardown cannot escape.
    //
    // Alive-token semantics: the failure path destroys NO fiber and publishes no new
    // subtree (tree work happens only after success), so every live fiber and its
    // alive token (captured by useState setters) survives.
    std::vector<SubscriptionRecord> oldSubs = std::move(fiber->subscriptionCleanups);
    fiber->subscriptionCleanups.clear();  // defensive: moved-from vector is unspecified

    const size_t preEffects = fiber->pendingEffects.size();  // 0 in steady state

    // Clear dirty BEFORE the render, not after. If the render re-dirties itself
    // (its own useState setter or a Store::set() targeting this fiber runs during
    // the render body), that mid-render re-dirty must survive: leaving dirty as the
    // render left it means a self-triggered update is picked up next frame instead
    // of being clobbered by an unconditional post-success clear.
    fiber->dirty.store(false, std::memory_order_relaxed);

    VNode result;
    bool threw = false;
    {
        FiberRenderContext fiberCtx(fiber);
        ComponentContext ctx(fiber, host_);
        try {
            result = fiber->componentFn(ctx);
        } catch (const std::exception& e) {
            threw = true;
            reportError("rerenderComponent", &e);
        } catch (...) {
            threw = true;
            reportError("rerenderComponent", nullptr);
        }
    }

    if (threw) {
        // Subscription-neutral rollback. Undo only the memberships this partial
        // render newly added (its fresh records' unsubscribe), re-arm the pre-render
        // memberships (the old records' resubscribe), then reinstate the old records.
        for (auto& fresh : fiber->subscriptionCleanups) {
            if (fresh.unsubscribe) fresh.unsubscribe();
        }
        for (auto& old : oldSubs) {
            if (old.resubscribe) old.resubscribe();
        }
        fiber->subscriptionCleanups = std::move(oldSubs);
        fiber->pendingEffects.resize(preEffects);
        // dirty was cleared before the render; re-arm so the failed pass is retried
        // next frame. markComponentDirty is the established release store the
        // host's acquire-load pairs with (do NOT call fiber->markDirty() — that
        // would re-set dirty AND markComponentDirty, doubling the mark).
        fiber->dirty.store(true, std::memory_order_relaxed);
        if (fiber->host) fiber->host->markComponentDirty();
        return;  // dirty re-armed; children untouched
    }

    // Render succeeded. Tear down stale subscriptions: run the old records whose
    // store the current render did NOT re-use; drop (without running) the old
    // records whose store the render re-used, since the fresh records already hold
    // their live membership. The fresh records remain the fiber's subscription list.
    std::unordered_set<const void*> currentStores;
    currentStores.reserve(fiber->subscriptionCleanups.size());
    for (auto& fresh : fiber->subscriptionCleanups) {
        currentStores.insert(fresh.store);
    }
    for (auto& old : oldSubs) {
        if (currentStores.find(old.store) == currentStores.end() && old.unsubscribe) {
            old.unsubscribe();
        }
    }

    // dirty is NOT touched here: it was cleared before the render, so leave it as
    // the render left it. A render that re-dirtied itself (via markDirty()) also
    // re-armed componentsDirty_ (release store), so next frame's single acquire-
    // load pass picks it up. In steady state the render does not re-dirty, dirty
    // stays cleared, componentsDirty_ is not re-armed, and the pass terminates.

    // Defer effects to the commit phase (drains after the whole pass, so effects
    // observe the fully-reconciled tree and any refs bound during the pass). The
    // fiber remains owned by the live tree, so a raw pointer is safe here.
    commit_.effects.push_back(fiber);

    Node* renderParent = findRenderParent(fiber);

    if (result.isEmpty) {
        // Remove render nodes + record cleanup order inline, then TRANSFER ownership
        // to the commit queue so the subtrees outlive children.clear() and their
        // deferred cleanups can run at drain (raw pointers in cleanupOrder stay valid).
        for (auto& child : fiber->children) {
            unmountFiber(child.get(), renderParent);
            commit_.removedRoots.push_back(std::move(child));
        }
        fiber->children.clear();
    } else if (!fiber->children.empty()) {
        auto& existingChild = fiber->children[0];
        if (existingChild->isHost() && existingChild->renderNode &&
            existingChild->renderNode->type() == result.type()) {
            // result.type() peeked above; safe to move the payload in now.
            reconcileHost(existingChild.get(), std::move(result));
        } else {
            unmountFiber(existingChild.get(), renderParent);
            commit_.removedRoots.push_back(std::move(existingChild));
            fiber->children.clear();

            size_t renderIndex = findRenderIndex(fiber);
            auto newChild = mountHost(std::move(result), 0, renderParent, renderIndex);
            if (newChild) {
                newChild->parent = fiber;
                fiber->children.push_back(std::move(newChild));
            }
        }
    } else {
        size_t renderIndex = findRenderIndex(fiber);
        auto newChild = mountHost(std::move(result), 0, renderParent, renderIndex);
        if (newChild) {
            newChild->parent = fiber;
            fiber->children.push_back(std::move(newChild));
        }
    }
}

// ============================================================================
// Render tree manipulation
// ============================================================================

Node* Reconciler::findRenderParent(Fiber* fiber) {
    Fiber* current = fiber->parent;
    while (current) {
        if (current->isHost() && current->renderNode) {
            return current->renderNode;
        }
        current = current->parent;
    }
    return renderRoot_.get();
}

size_t Reconciler::countRenderNodes(Fiber* fiber) {
    if (fiber->isHost()) return 1;
    size_t count = 0;
    for (auto& child : fiber->children) {
        count += countRenderNodes(child.get());
    }
    return count;
}

size_t Reconciler::findRenderIndex(Fiber* fiber) {
    size_t index = 0;
    Fiber* current = fiber;

    while (current->parent && current->parent->isComponent()) {
        Fiber* compParent = current->parent;
        for (auto& sibling : compParent->children) {
            if (sibling.get() == current) break;
            index += countRenderNodes(sibling.get());
        }
        current = compParent;
    }

    if (current->parent) {
        for (auto& sibling : current->parent->children) {
            if (sibling.get() == current) break;
            index += countRenderNodes(sibling.get());
        }
    }

    return index;
}

// Does this render parent keep its children OUT of the parent Yoga tree?
// Scroll and Portal content roots are laid out separately against their own
// constraint (see layoutDetachedContent in Node.cpp) rather than by the
// parent's flex flow — the ONE detachment gate both insertion paths share.
static bool detachesChildren(const Node* node) {
    return node->type() == PrimitiveType::Scroll || node->type() == PrimitiveType::Portal;
}

void Reconciler::insertRenderNode(Node* renderParent, std::unique_ptr<Node> node, size_t index) {
    if (!renderParent) return;

    node->parent = renderParent;

    // Insert into yoga tree (unless the parent detaches its content)
    if (!detachesChildren(renderParent) && renderParent->yogaNode && node->yogaNode) {
        YGNodeInsertChild(renderParent->yogaNode, node->yogaNode, index);
    }

    if (index >= renderParent->children.size()) {
        renderParent->children.push_back(std::move(node));
    } else {
        renderParent->children.insert(renderParent->children.begin() + index, std::move(node));
    }
}

void Reconciler::removeRenderNode(Node* renderParent, Node* node) {
    if (!renderParent || !node) return;

    if (renderParent->yogaNode && node->yogaNode) {
        YGNodeRemoveChild(renderParent->yogaNode, node->yogaNode);
    }

    for (auto it = renderParent->children.begin(); it != renderParent->children.end(); ++it) {
        if (it->get() == node) {
            renderParent->children.erase(it);
            return;
        }
    }
}

void Reconciler::collectRenderNodes(Fiber* fiber, std::vector<Node*>& out) {
    for (auto& child : fiber->children) {
        if (child->isHost() && child->renderNode) {
            out.push_back(child->renderNode);
        } else if (child->isComponent()) {
            collectRenderNodes(child.get(), out);
        }
    }
}

void Reconciler::rebuildRenderChildren(Fiber* parentFiber, Node* renderParent) {
    if (!renderParent) return;

    // Collect desired render node order from fiber children
    std::vector<Node*> desiredOrder;
    collectRenderNodes(parentFiber, desiredOrder);

    // Remove all yoga children
    if (renderParent->yogaNode) {
        YGNodeRemoveAllChildren(renderParent->yogaNode);
    }

    // Build a map from raw pointer to owned unique_ptr
    std::unordered_map<Node*, std::unique_ptr<Node>> nodeMap;
    for (auto& child : renderParent->children) {
        Node* raw = child.get();
        nodeMap[raw] = std::move(child);
    }
    renderParent->children.clear();

    // Re-insert in the correct order
    bool detached = detachesChildren(renderParent);
    size_t yogaIndex = 0;

    for (Node* desired : desiredOrder) {
        auto it = nodeMap.find(desired);
        if (it != nodeMap.end()) {
            auto& node = it->second;
            node->parent = renderParent;
            if (!detached && renderParent->yogaNode && node->yogaNode) {
                YGNodeInsertChild(renderParent->yogaNode, node->yogaNode, yogaIndex++);
            }
            renderParent->children.push_back(std::move(node));
            nodeMap.erase(it);
        }
    }
}

// ============================================================================
// Cleanup
// ============================================================================

void Reconciler::unmountFiber(Fiber* fiber, Node* renderParent) {
    if (!fiber) return;
    // Two phases, split so cleanups run AFTER the pass (defect D):
    //   1. Remove render nodes INLINE (correct layout during the pass).
    //   2. RECORD child-first cleanup order into the commit queue (run at drain).
    // Ownership of the removed subtree is transferred to commit_.removedRoots by the
    // caller, keeping the pointers enqueueCleanups records valid until drainCommit.
    removeRenderSubtree(fiber, renderParent);
    enqueueCleanups(fiber);
}

void Reconciler::removeRenderSubtree(Fiber* fiber, Node* renderParent) {
    if (!fiber) return;

    if (fiber->isHost() && fiber->renderNode) {
        // Removing the host render node cascade-destroys its render descendants, so
        // host children need no further render removal — but component-fiber children
        // hold their own render nodes under this renderParent and still do.
        notifyRenderRemoved(fiber->renderNode);
        removeRenderNode(renderParent, fiber->renderNode);
        fiber->renderNode = nullptr;
        for (auto& child : fiber->children) {
            if (child && child->isComponent()) removeRenderSubtree(child.get(), renderParent);
        }
    } else if (fiber->isComponent()) {
        for (auto& child : fiber->children) {
            removeRenderSubtree(child.get(), renderParent);
        }
    }
}

void Reconciler::enqueueCleanups(Fiber* fiber) {
    if (!fiber) return;
    // Child-first: recurse into children before recording self, so drainCommit runs
    // a child's cleanup before its parent's (React unmount order).
    for (auto& child : fiber->children) {
        enqueueCleanups(child.get());
    }
    commit_.cleanupOrder.push_back(fiber);
}

void Reconciler::drainCommit() {
    // Removed fibers' cleanups first (already child-first), then release the owned
    // subtrees; then mounted/updated effects in mount order. Effects run last so a
    // mounted component's effect never observes a not-yet-cleaned predecessor's
    // resource (defect C: cleanup-before-effect across a same-slot swap).
    for (Fiber* f : commit_.cleanupOrder) {
        if (f) f->runCleanups();
    }
    commit_.cleanupOrder.clear();
    commit_.removedRoots.clear();

    for (Fiber* f : commit_.effects) {
        if (f) f->runPendingEffects();
    }
    commit_.effects.clear();
}

void Reconciler::discardCommit() {
    // A pass threw partway through, leaving the tree half-built. Running user
    // effects/cleanups now is dangerous (they may touch fibers whose subtree
    // failed to mount), and — critically — the effects/cleanupOrder vectors may
    // hold raw pointers to fibers already destroyed by the unwinding pass. Clear
    // everything WITHOUT invoking any callback. removedRoots owns its subtrees, so
    // clearing it frees them; their deferred cleanups are discarded — the safe
    // choice for a failed pass. This guarantees no dangling pointer survives into
    // the next top-level pass's drain.
    commit_.cleanupOrder.clear();
    commit_.removedRoots.clear();
    commit_.effects.clear();
}

void Reconciler::notifyRenderRemoved(Node* node) {
    if (!node) return;

    if (onNodeRemoved_) {
        onNodeRemoved_(node);
    }

    for (auto& child : node->children) {
        notifyRenderRemoved(child.get());
    }
}

void Reconciler::reportError(std::string_view where, const std::exception* eOrNull) noexcept {
    if (host_) {
        host_->reportError(where, eOrNull);
    }
}

}  // namespace yui
