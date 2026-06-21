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

static std::string getStringKey(const Child& child) {
    return std::visit(
        [](const auto& c) -> std::string { return c.key; }, child);
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
            return fiber.isComponent();
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

std::unique_ptr<Fiber> Reconciler::mount(const VNode& vnode) {
    if (vnode.isEmpty) {
        return nullptr;
    }

    // The root is always a host node. Mount it without a render parent
    // (the render root is stored in renderRoot_).
    auto rootNode = createNode(vnode.type(), config_);
    rootNode->updateProps(vnode.props);
    rootNode->key = vnode.key;
    rootNode->intKey = vnode.intKey;
    bindRefSlot(rootNode.get(), vnode);

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
        const auto& child = vnode.children[i];
        if (isChildEmpty(child)) continue;

        auto childFiber = mountChild(child, i, fiber->renderNode, renderIndex);
        if (childFiber) {
            childFiber->parent = fiber.get();
            fiber->children.push_back(std::move(childFiber));
        }
    }

    return fiber;
}

std::unique_ptr<Fiber> Reconciler::mountHost(const VNode& vnode, size_t sourcePos,
                                              Node* renderParent, size_t& renderIndex) {
    if (vnode.isEmpty) return nullptr;

    auto fiber = std::make_unique<Fiber>();
    fiber->tag = Fiber::Tag::Host;
    fiber->key = vnode.key;
    fiber->intKey = vnode.intKey;
    fiber->sourcePosition = sourcePos;

    // Create render node
    auto node = createNode(vnode.type(), config_);
    node->updateProps(vnode.props);
    node->key = vnode.key;
    node->intKey = vnode.intKey;
    bindRefSlot(node.get(), vnode);
    fiber->renderNode = node.get();

    // Check for autoFocus before moving ownership
    bool wantsAutoFocus = false;
    if (vnode.type() == PrimitiveType::Input) {
        const auto& inputProps = std::get<InputProps>(vnode.props);
        wantsAutoFocus = inputProps.autoFocus.value_or(false);
    }

    // Insert into render tree
    insertRenderNode(renderParent, std::move(node), renderIndex);
    renderIndex++;

    // Notify host of autoFocus request
    if (wantsAutoFocus && onAutoFocus_) {
        onAutoFocus_(static_cast<InputNode*>(fiber->renderNode));
    }

    // Mount children — this fiber's renderNode is the render parent for children
    size_t childRenderIndex = 0;
    for (size_t i = 0; i < vnode.children.size(); ++i) {
        const auto& child = vnode.children[i];
        if (isChildEmpty(child)) continue;

        auto childFiber = mountChild(child, i, fiber->renderNode, childRenderIndex);
        if (childFiber) {
            childFiber->parent = fiber.get();
            fiber->children.push_back(std::move(childFiber));
        }
    }

    return fiber;
}

std::unique_ptr<Fiber> Reconciler::mountComponent(const Component& comp, size_t sourcePos,
                                                    Node* renderParent, size_t& renderIndex) {
    auto fiber = std::make_unique<Fiber>();
    fiber->tag = Fiber::Tag::Component;
    fiber->key = comp.key;
    fiber->intKey = comp.intKey;
    fiber->sourcePosition = sourcePos;
    fiber->componentFn = comp.fn;
    fiber->host = host_;

    fiber->debugName = comp.debugName;  // unconditional: feeds always-on hook diagnostics

    // Call the component function. If it throws, discard this never-published
    // fiber (its subscriptions self-unsubscribe via ~Fiber's cleanup path) and
    // mount nothing — no live fiber exists to corrupt. Returning nullptr matches
    // the empty-result contract: the caller simply attaches no child.
    VNode result;
    {
        FiberRenderContext fiberCtx(fiber.get());
        ComponentContext ctx(fiber.get(), host_);
        try {
            result = comp.fn(ctx);
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

    // Run pending effects (each effect body isolated in runPendingEffects)
    fiber->runPendingEffects();

    // Mount the result — render parent is the SAME as ours (component is invisible)
    if (!result.isEmpty) {
        auto childFiber = mountHost(result, 0, renderParent, renderIndex);
        if (childFiber) {
            childFiber->parent = fiber.get();
            fiber->children.push_back(std::move(childFiber));
        }
    }

    return fiber;
}

std::unique_ptr<Fiber> Reconciler::mountChild(const Child& child, size_t sourcePos,
                                                Node* renderParent, size_t& renderIndex) {
    if (isChildEmpty(child)) return nullptr;

    if (std::holds_alternative<VNode>(child)) {
        return mountHost(std::get<VNode>(child), sourcePos, renderParent, renderIndex);
    } else {
        return mountComponent(std::get<Component>(child), sourcePos, renderParent, renderIndex);
    }
}

// ============================================================================
// Reconciliation
// ============================================================================

void Reconciler::reconcile(Fiber* fiber, const VNode& vnode) {
    if (!fiber || vnode.isEmpty) return;

    // Must be a host fiber
    if (!fiber->isHost() || !fiber->renderNode) return;

    // Root primitive type changed between frames (the public render API allows
    // returning a different root each frame). updateProps would std::get<> the
    // wrong props variant and throw, so remount instead — mirroring the child
    // path / rerenderComponent, which remount on type mismatch.
    if (fiber->renderNode->type() != vnode.type()) {
        remountRoot(fiber, vnode);
        return;
    }

    // Update props on the render node
    fiber->renderNode->updateProps(vnode.props);
    bindRefSlot(fiber->renderNode, vnode);

    // Reconcile children
    reconcileChildren(fiber, vnode.children, fiber->renderNode);
}

void Reconciler::remountRoot(Fiber* fiber, const VNode& vnode) {
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

    auto fresh = mount(vnode);          // populates renderRoot_ with the new tree
    auto freshRenderRoot = takeRenderRoot();

    // The new tree is built and safely parked in freshRenderRoot. Only now tear
    // down the old root subtree's lifecycle (effects/subscriptions) and notify the
    // host that its render nodes are gone. The root render node has no render
    // parent, so it is dropped via ownership (oldRenderRoot) rather than
    // removeRenderNode.
    notifyRenderRemoved(fiber->renderNode);
    fiber->willUnmount();
    oldRenderRoot.reset();

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

void Reconciler::reconcileHost(Fiber* fiber, const VNode& vnode) {
    if (!fiber || !fiber->isHost() || !fiber->renderNode) return;

    // Update props
    fiber->renderNode->updateProps(vnode.props);
    bindRefSlot(fiber->renderNode, vnode);

    // Update identity
    fiber->key = vnode.key;
    fiber->intKey = vnode.intKey;

    // Reconcile children
    reconcileChildren(fiber, vnode.children, fiber->renderNode);
}

void Reconciler::reconcileComponent(Fiber* fiber, const Component& comp) {
    if (!fiber || !fiber->isComponent()) return;
    fiber->componentFn = comp.fn;
    rerenderComponent(fiber);
}

void Reconciler::reconcileChildren(Fiber* parentFiber, const std::vector<Child>& children,
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
        const auto& child = children[i];
        if (isChildEmpty(child)) continue;

        size_t existingIndex = lookup.find(child, i, parentFiber->children, reused);

        if (existingIndex != SIZE_MAX) {
            // Reuse existing fiber — mutate through the raw pointer, leaving the
            // unique_ptr owned in place in parentFiber->children.
            reused[existingIndex] = true;
            auto& existingFiber = parentFiber->children[existingIndex];
            existingFiber->sourcePosition = i;

            // Update keys
            if (hasIntKey(child)) existingFiber->intKey = getIntKey(child);
            if (hasStringKey(child)) existingFiber->key = getStringKey(child);

            // Reconcile based on type
            if (std::holds_alternative<VNode>(child)) {
                reconcileHost(existingFiber.get(), std::get<VNode>(child));
            } else {
                reconcileComponent(existingFiber.get(), std::get<Component>(child));
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
            auto newFiber = mountChild(child, i, renderParent, dummyRenderIndex);
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

    // Unmount fibers that weren't reused. The live vector is still fully populated
    // here, so these reads are safe and unmount happens through the real owned
    // fibers — preserving the normal unmount/destruction path (~Fiber clears alive).
    for (size_t i = 0; i < parentFiber->children.size(); ++i) {
        if (!reused[i] && parentFiber->children[i]) {
            unmountFiber(parentFiber->children[i].get(), renderParent);
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
    if (!fiber) return false;

    bool anyReconciled = false;

    if (fiber->isComponent() && fiber->dirty.load(std::memory_order_relaxed)) {
        // Re-render this component (which reconciles its entire subtree),
        // so we don't walk its children — they're handled by rerenderComponent.
        rerenderComponent(fiber);
        anyReconciled = true;
    } else {
        for (auto& child : fiber->children) {
            if (reconcileDirtyComponents(child.get())) {
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
        return;  // dirty stays set; children untouched
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

    // dirty is cleared here (after success), not before, so a failed pass leaves it set.
    fiber->dirty.store(false, std::memory_order_relaxed);

    // Run pending effects
    fiber->runPendingEffects();

    Node* renderParent = findRenderParent(fiber);

    if (result.isEmpty) {
        for (auto& child : fiber->children) {
            unmountFiber(child.get(), renderParent);
        }
        fiber->children.clear();
    } else if (!fiber->children.empty()) {
        auto& existingChild = fiber->children[0];
        if (existingChild->isHost() && existingChild->renderNode &&
            existingChild->renderNode->type() == result.type()) {
            reconcileHost(existingChild.get(), result);
        } else {
            unmountFiber(existingChild.get(), renderParent);
            fiber->children.clear();

            size_t renderIndex = findRenderIndex(fiber);
            auto newChild = mountHost(result, 0, renderParent, renderIndex);
            if (newChild) {
                newChild->parent = fiber;
                fiber->children.push_back(std::move(newChild));
            }
        }
    } else {
        size_t renderIndex = findRenderIndex(fiber);
        auto newChild = mountHost(result, 0, renderParent, renderIndex);
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

void Reconciler::insertRenderNode(Node* renderParent, std::unique_ptr<Node> node, size_t index) {
    if (!renderParent) return;

    node->parent = renderParent;

    // Insert into yoga tree (unless parent is ScrollNode)
    if (renderParent->type() != PrimitiveType::Scroll && renderParent->yogaNode && node->yogaNode) {
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
    bool isScroll = (renderParent->type() == PrimitiveType::Scroll);
    size_t yogaIndex = 0;

    for (Node* desired : desiredOrder) {
        auto it = nodeMap.find(desired);
        if (it != nodeMap.end()) {
            auto& node = it->second;
            node->parent = renderParent;
            if (!isScroll && renderParent->yogaNode && node->yogaNode) {
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

    if (fiber->isHost() && fiber->renderNode) {
        notifyRenderRemoved(fiber->renderNode);
        removeRenderNode(renderParent, fiber->renderNode);
        fiber->renderNode = nullptr;
    }

    if (fiber->isComponent()) {
        // Component: per-fiber cleanup, then recurse via unmountFiber
        // (children share renderParent, need individual render node removal)
        fiber->runCleanups();
        for (auto& child : fiber->children) {
            unmountFiber(child.get(), renderParent);
        }
    } else {
        // Host: render nodes already cascade-destroyed by removeRenderNode
        // willUnmount handles recursive lifecycle cleanup for all descendants
        fiber->willUnmount();
    }
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
