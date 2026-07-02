#include "yui/core/NodeRef.hpp"

#include "yui/core/Host.hpp"  // currentRenderFiber / currentRenderHost
#include "yui/core/Node.hpp"  // Node, ScrollNode, layout offsets

namespace yui {

layout::Rect absoluteRect(const Node* node) {
    if (!node)
        return {};

    // Accumulate this node's own offset within its parent, then walk up: each
    // ancestor contributes its layout offset, and a ScrollNode ancestor
    // additionally subtracts its scroll offset (content shifts under the
    // viewport). Mirrors hitTest's top-down accumulation, run bottom-up.
    float x = node->layout.left;
    float y = node->layout.top;

    const Node* cur = node->parent;
    while (cur) {
        if (cur->type() == PrimitiveType::Scroll) {
            // Scroll content lives at the PADDED viewport origin, shifted by
            // the scroll offset — the same accumulation hitTest and drawScroll
            // use, so the drawn rect matches where the node is hit and painted.
            const auto* scroll = static_cast<const ScrollNode*>(cur);
            x += cur->layout.insetLeft - scroll->scrollOffsetX;
            y += cur->layout.insetTop - scroll->scrollOffsetY;
        }
        x += cur->layout.left;
        y += cur->layout.top;
        cur = cur->parent;
    }

    return {x, y, node->layout.width, node->layout.height};
}

layout::Rect NodeRef::getBoundingRect() const {
    return absoluteRect(current());
}

ScrollNode* NodeRef::asScroll() const {
    Node* n = current();
    if (!n || n->type() != PrimitiveType::Scroll)
        return nullptr;
    return static_cast<ScrollNode*>(n);
}

bool NodeRef::inRenderPhase() {
    return currentRenderFiber != nullptr || currentRenderHost != nullptr;
}

}  // namespace yui
