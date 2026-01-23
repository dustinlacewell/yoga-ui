#pragma once

#include "Node.hpp"
#include "VNode.hpp"

#include <functional>
#include <memory>

namespace yui {

class Reconciler {
public:
    // Callback invoked when a node is about to be removed from the tree
    // Called before the node is destroyed, for each node in the removed subtree
    using NodeRemovedCallback = std::function<void(Node*)>;

    void setNodeRemovedCallback(NodeRemovedCallback callback) { onNodeRemoved_ = std::move(callback); }

    // Mount a VNode tree, creating the initial Node tree
    // sourcePosition is used for position-based matching during reconciliation
    std::unique_ptr<Node> mount(const VNode& vnode, size_t sourcePosition = 0);

    // Reconcile an existing Node tree against a new VNode tree
    void reconcile(Node* node, const VNode& vnode);

private:
    // Reconcile children of a node
    void reconcileChildren(Node* parent, const std::vector<VNode>& vnodeChildren);

    // Insert a child node into parent's yoga tree
    void insertYogaChild(Node* parent, Node* child, size_t index);

    // Remove a child node from parent's yoga tree
    void removeYogaChild(Node* parent, Node* child);

    // Notify callback for a node and all its descendants being removed
    void notifyRemoved(Node* node);

    NodeRemovedCallback onNodeRemoved_;
};

}  // namespace yui
