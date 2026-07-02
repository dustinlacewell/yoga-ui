#pragma once

#include <yui/core/Fiber.hpp>
#include <yui/core/Node.hpp>
#include <yui/core/VNode.hpp>

#include <exception>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

#include <yoga/Yoga.h>

namespace yui {

struct DirtyScheduler;

class Reconciler {
public:
    using NodeRemovedCallback = std::function<void(Node*)>;
    using AutoFocusCallback = std::function<void(InputNode*)>;

    void setNodeRemovedCallback(NodeRemovedCallback callback) { onNodeRemoved_ = std::move(callback); }
    void setAutoFocusCallback(AutoFocusCallback callback) { onAutoFocus_ = std::move(callback); }
    void setHost(DirtyScheduler* host) { host_ = host; }

    // The yoga config that render nodes are created against. Carries the host's
    // text measurer in its context (see Host::setTextMeasurer). nullptr resolves
    // to Yoga's default config.
    void setConfig(YGConfigRef config) { config_ = config; }

    // Mount a VNode tree -> creates fiber tree AND render tree.
    // Returns the root fiber. The render tree root is stored internally.
    // Top-level entry: drains the commit queue (mount effects) before returning.
    // Takes the VNode by rvalue: props are moved through the whole pipeline into
    // the render nodes (the VNode is fully consumed by reconcile, never read after).
    std::unique_ptr<Fiber> mount(VNode&& vnode);

    // Reconcile existing fiber tree against new VNode tree.
    // Top-level entry: drains the commit queue before returning.
    void reconcile(Fiber* fiber, VNode&& vnode);

    // Walk fiber tree and re-render any dirty components.
    // Returns true if any components were re-rendered.
    // Top-level entry: drains the commit queue before returning.
    bool reconcileDirtyComponents(Fiber* fiber);

    // Access the render tree root
    Node* renderRoot() const { return renderRoot_.get(); }

    // Take ownership of render root (for Host)
    std::unique_ptr<Node> takeRenderRoot() { return std::move(renderRoot_); }

private:
    // --- Top-level impls (no drain; the public wrappers drain) ---
    std::unique_ptr<Fiber> mountImpl(VNode&& vnode);
    void reconcileImpl(Fiber* fiber, VNode&& vnode);
    bool reconcileDirtyComponentsImpl(Fiber* fiber);

    // --- Mounting ---
    std::unique_ptr<Fiber> mountHost(VNode&& vnode, size_t sourcePos,
                                     Node* renderParent, size_t& renderIndex);

    std::unique_ptr<Fiber> mountComponent(Component&& comp, size_t sourcePos,
                                          Node* renderParent, size_t& renderIndex);

    std::unique_ptr<Fiber> mountChild(Child&& child, size_t sourcePos,
                                      Node* renderParent, size_t& renderIndex);

    // --- Reconciliation ---
    // Tear down the existing root fiber/render node and rebuild from a new VNode
    // whose root primitive type differs from the current one. Mirrors the
    // remount-on-type-mismatch performed by the child path / rerenderComponent.
    void remountRoot(Fiber* fiber, VNode&& vnode);
    void reconcileHost(Fiber* fiber, VNode&& vnode);
    void reconcileComponent(Fiber* fiber, Component&& comp);
    void reconcileChildren(Fiber* parentFiber, std::vector<Child>&& children,
                           Node* renderParent);

    // --- Dirty component re-render ---
    void rerenderComponent(Fiber* fiber);

    // --- Render tree manipulation ---
    Node* findRenderParent(Fiber* fiber);
    size_t findRenderIndex(Fiber* fiber);
    void insertRenderNode(Node* renderParent, std::unique_ptr<Node> node, size_t index);
    void removeRenderNode(Node* renderParent, Node* node);
    void rebuildRenderChildren(Fiber* parentFiber, Node* renderParent);

    // Collect render nodes from fiber children in order (flattening components)
    void collectRenderNodes(Fiber* fiber, std::vector<Node*>& out);

    // Count render nodes contributed by a fiber subtree
    static size_t countRenderNodes(Fiber* fiber);

    // --- Cleanup ---
    void unmountFiber(Fiber* fiber, Node* renderParent);
    // Remove a fiber subtree's render nodes INLINE (during the reconcile pass), no
    // lifecycle cleanup — that is deferred via enqueueCleanups.
    void removeRenderSubtree(Fiber* fiber, Node* renderParent);
    void notifyRenderRemoved(Node* node);

    // --- Commit phase (deferred lifecycle) ---
    // React model: reconcile builds the tree during the pass; a COMMIT phase then
    // runs removed fibers' cleanups (child-first) and mounted/updated fibers'
    // effects (mount order) AFTER the pass. Deferring matters because effects and
    // cleanups may observe/mutate shared state and read element refs bound during
    // the pass.
    //
    // Ownership subtlety: reconcileChildren destroys non-reused fibers during the
    // pass (the children-vector swap drops their unique_ptrs). A deferred cleanup
    // on such a fiber would be a use-after-free, so removedRoots OWNS the removed
    // subtrees until drainCommit. Effects run on fibers still owned by the LIVE
    // tree, so raw pointers are safe there.
    struct CommitQueue {
        std::vector<std::unique_ptr<Fiber>> removedRoots;  // owns removed subtrees until drain
        std::vector<Fiber*> cleanupOrder;                  // child-first; points into removedRoots
        std::vector<Fiber*> effects;                       // mounted/updated (owned by live tree)
    };
    CommitQueue commit_;

    // Run removed fibers' cleanups (child-first), then mounted/updated effects
    // (mount order), then clear the queue. Called once per top-level pass on
    // NORMAL exit.
    void drainCommit();
    // Clear the queue WITHOUT running any user callbacks. Called on EXCEPTION
    // exit from a pass: the tree is half-built, so effects/cleanups must not run
    // (they may touch fibers whose subtree failed to mount), and no raw pointer
    // may be left in commit_ for a later drain to fault on.
    void discardCommit();
    // Record child-first cleanup order for a removed subtree (does NOT transfer
    // ownership — reconcileChildren moves the owning unique_ptr into removedRoots).
    void enqueueCleanups(Fiber* fiber);

    // --- Diagnostics ---
    // Route a caught user-callback exception to the scheduler's sink via the
    // existing host_ back-reference. No-op when host-less (reconciler-only unit
    // tests).
    void reportError(std::string_view where, const std::exception* eOrNull) noexcept;

    // --- State ---
    std::unique_ptr<Node> renderRoot_;
    NodeRemovedCallback onNodeRemoved_;
    AutoFocusCallback onAutoFocus_;
    DirtyScheduler* host_ = nullptr;
    YGConfigRef config_ = nullptr;
};

}  // namespace yui
