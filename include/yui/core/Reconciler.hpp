#pragma once

#include "Fiber.hpp"
#include "Node.hpp"
#include "VNode.hpp"

#include <functional>
#include <memory>

#include <yoga/Yoga.h>

namespace yui {

class Host;

class Reconciler {
public:
    using NodeRemovedCallback = std::function<void(Node*)>;

    void setNodeRemovedCallback(NodeRemovedCallback callback) { onNodeRemoved_ = std::move(callback); }
    void setHost(Host* host) { host_ = host; }

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

    // --- State ---
    std::unique_ptr<Node> renderRoot_;
    NodeRemovedCallback onNodeRemoved_;
    Host* host_ = nullptr;
};

}  // namespace yui
