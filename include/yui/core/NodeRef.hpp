#pragma once

#include "../layout/Placement.hpp"  // layout::Rect (return type of getBoundingRect)

#include <memory>

namespace yui {

class Node;

// Backing slot for an element ref. Holds a non-owning pointer to the render Node
// the ref is attached to, plus a weak copy of that node's liveness token
// (Node::alive). The slot lives in a shared_ptr whose control block is stable
// across renders (stored once in the fiber's hookState by useElementRef), so a
// consumer that captures a NodeRef into a closure observes the same slot every
// render — the React useRef identity-stability guarantee.
struct NodeRefSlot {
    Node* node = nullptr;
    std::weak_ptr<bool> alive;
};

// A handle to a render node attached via BuilderBase::ref(). Mirrors React's
// element ref (const ref = useRef(); <div ref={ref}/>; ref.current):
//
//   - current() / get() returns the live Node*, or nullptr if the node was
//     reconciled away (lazy liveness check, mirroring liveFocusedInput) OR if
//     called during the render phase (matching React: refs are null during
//     render; read them only in handlers/effects against settled layout).
//   - getBoundingRect() returns the node's absolute drawn rect (scroll- and
//     clamp-correct), the analog of element.getBoundingClientRect(), for feeding
//     into yui::layout placement helpers.
//
// Copyable and cheap (one shared_ptr). The reconciler populates the slot when it
// binds the node to its VNode; population self-heals across remounts because the
// bind re-runs every render the element appears.
class NodeRef {
public:
    NodeRef() : slot_(std::make_shared<NodeRefSlot>()) {}

    // The live node, or nullptr. Null when: not yet mounted, reconciled away, or
    // called during render (see class note). Always guard the result.
    Node* current() const {
        if (inRenderPhase())
            return nullptr;
        if (!slot_->node)
            return nullptr;
        auto live = slot_->alive.lock();
        if (!live || !*live)
            return nullptr;
        return slot_->node;
    }

    // Alias for current(), for C++ smart-pointer muscle memory.
    Node* get() const { return current(); }

    // Absolute drawn rect of the live node, or an empty rect if current() is
    // null. Defined out-of-line (needs Node + the absoluteRect walk).
    layout::Rect getBoundingRect() const;

    explicit operator bool() const { return current() != nullptr; }

    // Internal: the reconciler binds the slot to a node during reconcile. Not
    // part of the consumer-facing surface.
    const std::shared_ptr<NodeRefSlot>& slot() const { return slot_; }

private:
    // True while a component/host render is in progress on this thread, so a
    // current() called from a component body returns null (React parity) rather
    // than a stale, last-frame node. Defined in NodeRef.cpp against the render-
    // context thread-locals the reconciler installs.
    static bool inRenderPhase();

    std::shared_ptr<NodeRefSlot> slot_;
};

// Absolute drawn rect of a node: walks parent->root accumulating layout
// offsets and subtracting each ScrollNode ancestor's scroll offset (mirrors the
// hit-test/render accumulation). Screen/viewport space. Free function — any
// Node* holder can call it, and it keeps layout/ decoupled from core Node types.
layout::Rect absoluteRect(const Node* node);

}  // namespace yui
