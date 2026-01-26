#include <yui/core/Reconciler.hpp>

#include <algorithm>
#include <unordered_map>

namespace yui {

std::unique_ptr<Node> Reconciler::mount(const VNode& vnode, size_t sourcePosition) {
    if (vnode.isEmpty) {
        return nullptr;
    }

    auto node = createNode(vnode.type);
    node->key = vnode.key;
    node->intKey = vnode.intKey;
    node->sourcePosition = sourcePosition;
    node->updateProps(vnode.props);

    // Mount children, tracking their source positions
    for (size_t i = 0; i < vnode.children.size(); ++i) {
        const auto& childVNode = vnode.children[i];
        if (childVNode.isEmpty)
            continue;

        auto childNode = mount(childVNode, i);  // Pass VNode index as source position
        if (childNode) {
            childNode->parent = node.get();
            // ScrollNode children are NOT Yoga children - they're laid out separately
            // with unconstrained height so content can exceed the scroll viewport
            if (node->type() != PrimitiveType::Scroll) {
                insertYogaChild(node.get(), childNode.get(), node->children.size());
            }
            node->children.push_back(std::move(childNode));
        }
    }

    return node;
}

void Reconciler::reconcile(Node* node, const VNode& vnode) {
    if (!node || vnode.isEmpty)
        return;

    // Update props
    node->updateProps(vnode.props);

    // Reconcile children
    reconcileChildren(node, vnode.children);
}

// Helper to find existing child node that matches a VNode
struct ChildLookup {
    std::unordered_map<int64_t, size_t> byIntKey;
    std::unordered_map<std::string, size_t> byStringKey;
    std::unordered_map<size_t, size_t> byPosition;

    void build(const std::vector<std::unique_ptr<Node>>& children) {
        for (size_t i = 0; i < children.size(); ++i) {
            const auto& child = children[i];
            if (child->hasIntKey()) {
                byIntKey[child->intKey] = i;
            } else if (child->hasStringKey()) {
                byStringKey[child->key] = i;
            } else if (child->sourcePosition != Node::NO_SOURCE_POSITION) {
                byPosition[child->sourcePosition] = i;
            }
        }
    }

    // Returns index of matching child, or SIZE_MAX if not found
    size_t find(const VNode& vnode, size_t vnodeIndex,
                const std::vector<std::unique_ptr<Node>>& children,
                const std::vector<bool>& reused) const {
        // Try int key first (most efficient)
        if (vnode.hasIntKey()) {
            auto it = byIntKey.find(vnode.intKey);
            if (it != byIntKey.end() && children[it->second]->type() == vnode.type) {
                return it->second;
            }
        }
        // Fall back to string key
        else if (vnode.hasStringKey()) {
            auto it = byStringKey.find(vnode.key);
            if (it != byStringKey.end() && children[it->second]->type() == vnode.type) {
                return it->second;
            }
        }
        // Fall back to position match (only for keyless nodes)
        else {
            auto it = byPosition.find(vnodeIndex);
            if (it != byPosition.end() && !reused[it->second] &&
                children[it->second]->type() == vnode.type) {
                return it->second;
            }
        }
        return SIZE_MAX;
    }
};

void Reconciler::reconcileChildren(Node* parent, const std::vector<VNode>& vnodeChildren) {
    // Build lookup tables for existing children
    ChildLookup lookup;
    lookup.build(parent->children);

    // Track which old children are reused
    std::vector<bool> reused(parent->children.size(), false);

    // Track if children are unchanged (same nodes in same order)
    bool childrenUnchanged = true;
    size_t newChildIndex = 0;

    // New children list
    std::vector<std::unique_ptr<Node>> newChildren;

    for (size_t i = 0; i < vnodeChildren.size(); ++i) {
        const auto& childVNode = vnodeChildren[i];

        if (childVNode.isEmpty) {
            continue;
        }

        size_t existingIndex = lookup.find(childVNode, i, parent->children, reused);

        if (existingIndex != SIZE_MAX) {
            // Reuse existing node
            Node* existingChild = parent->children[existingIndex].get();
            reused[existingIndex] = true;
            existingChild->sourcePosition = i;
            existingChild->intKey = childVNode.intKey;

            reconcile(existingChild, childVNode);
            newChildren.push_back(std::move(parent->children[existingIndex]));

            if (existingIndex != newChildIndex) {
                childrenUnchanged = false;
            }
        } else {
            // Create new node
            childrenUnchanged = false;

            auto newNode = mount(childVNode, i);
            if (newNode) {
                newNode->parent = parent;
                newChildren.push_back(std::move(newNode));
            }
        }
        newChildIndex++;
    }

    // Check if any children were removed
    if (newChildren.size() != parent->children.size()) {
        childrenUnchanged = false;
    }

    // Notify and unmount nodes that weren't reused
    for (size_t i = 0; i < parent->children.size(); ++i) {
        if (!reused[i] && parent->children[i]) {
            notifyRemoved(parent->children[i].get());
            parent->children[i]->willUnmount();
        }
    }

    // Only rebuild yoga child relationships if children actually changed
    if (!childrenUnchanged) {
        YGNodeRemoveAllChildren(parent->yogaNode);
        if (parent->type() != PrimitiveType::Scroll) {
            for (size_t i = 0; i < newChildren.size(); ++i) {
                YGNodeInsertChild(parent->yogaNode, newChildren[i]->yogaNode, i);
            }
        }
    }

    parent->children = std::move(newChildren);
}

void Reconciler::insertYogaChild(Node* parent, Node* child, size_t index) {
    YGNodeInsertChild(parent->yogaNode, child->yogaNode, index);
}

void Reconciler::removeYogaChild(Node* parent, Node* child) {
    YGNodeRemoveChild(parent->yogaNode, child->yogaNode);
}

void Reconciler::notifyRemoved(Node* node) {
    if (!node)
        return;

    // Notify for this node
    if (onNodeRemoved_) {
        onNodeRemoved_(node);
    }

    // Notify for all descendants
    for (auto& child : node->children) {
        notifyRemoved(child.get());
    }
}

}  // namespace yui
