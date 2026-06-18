#include <yui/core/Reconciler.hpp>

#include <yui/core/ComponentContext.hpp>
#include <yui/core/Host.hpp>

#include <algorithm>
#include <unordered_map>

namespace yui {

// --- Helpers for Child variant ---

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
            return fiber.renderNode->type() == std::get<VNode>(child).type;
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
    auto rootNode = createNode(vnode.type);
    rootNode->updateProps(vnode.props);
    rootNode->key = vnode.key;
    rootNode->intKey = vnode.intKey;

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
    auto node = createNode(vnode.type);
    node->updateProps(vnode.props);
    node->key = vnode.key;
    node->intKey = vnode.intKey;
    fiber->renderNode = node.get();

    // Check for autoFocus before moving ownership
    bool wantsAutoFocus = false;
    if (vnode.type == PrimitiveType::Input) {
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

#ifndef NDEBUG
    fiber->debugName = comp.debugName;
#endif

    // Set render context for Store subscriptions
    FiberRenderContext fiberCtx(fiber.get());

    // Call the component function
    ComponentContext ctx(fiber.get(), host_);
    VNode result = comp.fn(ctx);

    // Run pending effects
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
    if (fiber->renderNode->type() != vnode.type) {
        remountRoot(fiber, vnode);
        return;
    }

    // Update props on the render node
    fiber->renderNode->updateProps(vnode.props);

    // Reconcile children
    reconcileChildren(fiber, vnode.children, fiber->renderNode);
}

void Reconciler::remountRoot(Fiber* fiber, const VNode& vnode) {
    // Tear down the old root subtree's lifecycle (effects/subscriptions) and
    // notify the host that its render nodes are gone. The root render node has
    // no render parent, so we drop it via ownership rather than removeRenderNode.
    notifyRenderRemoved(fiber->renderNode);
    fiber->willUnmount();
    renderRoot_.reset();

    // Rebuild a fresh root fiber + render root from the new VNode, then move its
    // state into the existing root fiber so the host's fiberRoot_ pointer (and
    // any parent links into it) stay valid — no dangling, no leaks.
    auto fresh = mount(vnode);
    *fiber = std::move(*fresh);
    for (auto& child : fiber->children) {
        child->parent = fiber;
    }

    // Hand the newly-built render root to the host. After the first frame the
    // host owns renderRoot_ (via takeRenderRoot()), so without this it would keep
    // pointing at the freed old root. Reuses the existing host_ back-reference
    // rather than adding another reconciler callback.
    if (host_) {
        host_->replaceRenderRoot(std::move(renderRoot_));
    }
}

void Reconciler::reconcileHost(Fiber* fiber, const VNode& vnode) {
    if (!fiber || !fiber->isHost() || !fiber->renderNode) return;

    // Update props
    fiber->renderNode->updateProps(vnode.props);

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

    std::vector<std::unique_ptr<Fiber>> newFibers;

    for (size_t i = 0; i < children.size(); ++i) {
        const auto& child = children[i];
        if (isChildEmpty(child)) continue;

        size_t existingIndex = lookup.find(child, i, parentFiber->children, reused);

        if (existingIndex != SIZE_MAX) {
            // Reuse existing fiber
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

            newFibers.push_back(std::move(parentFiber->children[existingIndex]));

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
                newFibers.push_back(std::move(newFiber));
            }
        }
        newChildIndex++;
    }

    if (newFibers.size() != parentFiber->children.size()) {
        childrenUnchanged = false;
    }

    // Unmount fibers that weren't reused
    for (size_t i = 0; i < parentFiber->children.size(); ++i) {
        if (!reused[i] && parentFiber->children[i]) {
            unmountFiber(parentFiber->children[i].get(), renderParent);
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

    if (fiber->isComponent() && fiber->dirty) {
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

    fiber->dirty = false;

    // Clear old subscription cleanups
    for (auto& cleanup : fiber->subscriptionCleanups) {
        cleanup();
    }
    fiber->subscriptionCleanups.clear();

    // Set render context
    FiberRenderContext fiberCtx(fiber);

    // Call component function
    ComponentContext ctx(fiber, host_);
    VNode result = fiber->componentFn(ctx);

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
            existingChild->renderNode->type() == result.type) {
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

}  // namespace yui
