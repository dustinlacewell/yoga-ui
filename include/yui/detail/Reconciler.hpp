#pragma once

#include <yui/core/Fiber.hpp>
#include <yui/core/Node.hpp>
#include <yui/core/VNode.hpp>

#include <exception>
#include <functional>
#include <memory>
#include <string_view>

#include <yoga/Yoga.h>

namespace yui {

class DirtyScheduler;

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
    std::unique_ptr<Fiber> mount(const VNode& vnode);

    // Reconcile existing fiber tree against new VNode tree
    void reconcile(Fiber* fiber, const VNode& vnode);

    // Walk fiber tree and re-render any dirty components.
    // Returns true if any components were re-rendered.
    bool reconcileDirtyComponents(Fiber* fiber);

    // Access the render tree root
    Node* renderRoot() const { return renderRoot_.get(); }

    // Take ownership of render root (for Host)
    std::unique_ptr<Node> takeRenderRoot() { return std::move(renderRoot_); }

private:
    // --- Mounting ---
    std::unique_ptr<Fiber> mountHost(const VNode& vnode, size_t sourcePos,
                                     Node* renderParent, size_t& renderIndex);

    std::unique_ptr<Fiber> mountComponent(const Component& comp, size_t sourcePos,
                                          Node* renderParent, size_t& renderIndex);

    std::unique_ptr<Fiber> mountChild(const Child& child, size_t sourcePos,
                                      Node* renderParent, size_t& renderIndex);

    // --- Reconciliation ---
    // Tear down the existing root fiber/render node and rebuild from a new VNode
    // whose root primitive type differs from the current one. Mirrors the
    // remount-on-type-mismatch performed by the child path / rerenderComponent.
    void remountRoot(Fiber* fiber, const VNode& vnode);
    void reconcileHost(Fiber* fiber, const VNode& vnode);
    void reconcileComponent(Fiber* fiber, const Component& comp);
    void reconcileChildren(Fiber* parentFiber, const std::vector<Child>& children,
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
    void notifyRenderRemoved(Node* node);

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
