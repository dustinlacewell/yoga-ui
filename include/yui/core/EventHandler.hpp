#pragma once

#include "ErrorHandler.hpp"
#include "Event.hpp"
#include "Node.hpp"

#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace yui {

// Handles event dispatch and hit testing
class EventHandler {
public:
    // Install the diagnostic sink. Owned by Host; a throwing user handler is
    // caught at its call site and routed here so the dispatch walk / hover-focus
    // bookkeeping survives intact.
    void setErrorHandler(ErrorHandler handler) { errorHandler_ = std::move(handler); }

    // Process a mouse event at absolute coordinates.
    // Returns true if any node consumed the event. noexcept: these are reached
    // from the platform event loop; each user callback is isolated at its call
    // site (routed to the sink), so a normal handler throw never reaches here.
    bool handleMouseDown(Node* root, float x, float y, MouseButton button = MouseButton::Left) noexcept;
    bool handleMouseUp(Node* root, float x, float y, MouseButton button = MouseButton::Left) noexcept;
    bool handleMouseMove(Node* root, float x, float y) noexcept;
    bool handleScroll(Node* root, float x, float y, float deltaX, float deltaY) noexcept;

    // Keyboard events - dispatched to focused node (or root if none)
    bool handleKeyDown(Node* root, int keyCode, uint16_t keyMod, bool repeat = false) noexcept;
    bool handleKeyUp(Node* root, int keyCode, uint16_t keyMod) noexcept;

    // Text input for focused input node
    void handleTextInput(const std::string& text) noexcept;
    void handleBackspace() noexcept;
    void handleSubmit() noexcept;

    // Get the currently hovered node (for cursor changes, etc.). Validates the
    // liveness token first (mirrors getFocusedInput): a hovered node freed by a
    // reconciliation is reported as no-hover rather than a dangling pointer.
    Node* getHoveredNode() const { return liveHoveredNode(); }

    // Get the focused input node (for text input routing). Validates the
    // liveness token first: if the focused InputNode was freed by a
    // reconciliation, this clears the stale pointer and returns nullptr so a
    // caller never receives a dangling pointer.
    InputNode* getFocusedInput() const {
        return liveFocusedInput();
    }

    // Programmatically focus an input node (used by autoFocus)
    void focusInput(InputNode* node);

    // Read-and-clear the visual-state latch: true iff a hover, press, or focus
    // TRANSITION occurred since the last consume. Latched (not returned per
    // event) because handle*() may run between update() calls — the flag
    // persists until the next Host::update() folds it into needsRepaint. Any
    // transition marks, without per-node style introspection: a false-positive
    // repaint is cheap; a missed one is the stale-hover-style defect.
    bool consumeVisualStateChanged() noexcept { return std::exchange(visualStateChanged_, false); }

    // Override the depth at which the recursive event-path walks (hit-test,
    // dispatch bubble, key-target search) stop and diagnose instead of recursing
    // further. Defaults to kMaxTreeDepth. Exists so tests can exercise the guard
    // with a shallow tree (building a kMaxTreeDepth-deep tree overflows the
    // unguarded mount/layout/teardown recursion on a 1 MB stack); production never
    // changes it.
    void setMaxTreeDepth(int depth) { maxTreeDepth_ = depth; }

    // Check if node or ancestors have a click handler for the given button
    bool hasClickHandler(Node* root, float x, float y, MouseButton button = MouseButton::Left);

    // Called when a node is being removed from the tree
    // Clears any references to this node
    void onNodeRemoved(Node* node) {
        if (hoveredNode_ == node) {
            setHoveredNode(nullptr);
        }
        if (focusedInput_ == node) {
            setFocusedInput(nullptr);
        }
        if (pressedNode_ == node) {
            setPressedNode(nullptr);
        }
    }

    // Hit test: find deepest node containing point. `depth` bounds the descent
    // against maxTreeDepth_ (kMaxTreeDepth by default): a tree deeper than that
    // stops descending (treated as no deeper hit) and emits one diagnostic, rather
    // than overflowing the stack.
    Node* hitTest(Node* node, float x, float y, float offsetX = 0, float offsetY = 0, int depth = 0);

private:
    // Check if point is inside node bounds
    bool containsPoint(Node* node, float x, float y, float offsetX, float offsetY);

    // Dispatch event to node and ancestors (bubbling). `depth` bounds the bubble
    // walk against maxTreeDepth_ (see hitTest): an over-deep ancestor chain stops
    // bubbling and emits one diagnostic instead of overflowing the stack.
    bool dispatchEvent(Node* node, Event& event, int depth = 0);

    // Fire hover callbacks
    void updateHover(Node* newHovered);

    // Lowest common ancestor of two nodes (nullptr if either is null or the two
    // live under different roots). Used by updateHover to cut enter/leave at the
    // shared ancestor so a sibling-to-sibling move never re-fires onHover on nodes
    // both the old and new targets share. Depth-bounded by maxTreeDepth_ (mirrors
    // the other event-path walks: diagnose once and proceed on overflow).
    Node* lowestCommonAncestor(Node* a, Node* b);

    // Update focus when clicking
    void updateFocus(Node* clicked);

    // Set the focused input and capture its liveness token in lockstep. The
    // sole writer of focusedInput_ — keeps the raw pointer and the weak token
    // observing the same node so a later validate can detect a freed input.
    void setFocusedInput(InputNode* node) {
        focusedInput_ = node;
        focusedInputAlive_ = node ? node->alive : std::weak_ptr<bool>{};
    }

    // Return focusedInput_ only if its node is still alive; otherwise clear the
    // stale pointer and return nullptr. Every deref of the focused input routes
    // through here so a reconciliation that freed the node (without an
    // onNodeRemoved for it) can never produce a use-after-free.
    InputNode* liveFocusedInput() const {
        if (!focusedInput_)
            return nullptr;
        auto alive = focusedInputAlive_.lock();
        if (!alive || !*alive) {
            focusedInput_ = nullptr;
            focusedInputAlive_.reset();
            return nullptr;
        }
        return focusedInput_;
    }

    // Record the node that received the most recent mouse press, with its button,
    // capturing the liveness token in lockstep (mirrors setFocusedInput). The
    // MouseUp dispatch matches a click against this so a release with no matching
    // press on the same node fires nothing.
    void setPressedNode(Node* node, MouseButton button = MouseButton::Left) {
        pressedNode_ = node;
        pressedButton_ = button;
        pressedNodeAlive_ = node ? node->alive : std::weak_ptr<bool>{};
    }

    // Return pressedNode_ only if its node is still alive; otherwise clear the
    // stale pointer and return nullptr (mirrors liveFocusedInput — a node freed by
    // a reconciliation between press and release can never be dereferenced).
    Node* livePressedNode() const {
        if (!pressedNode_)
            return nullptr;
        auto alive = pressedNodeAlive_.lock();
        if (!alive || !*alive) {
            pressedNode_ = nullptr;
            pressedNodeAlive_.reset();
            return nullptr;
        }
        return pressedNode_;
    }

    // Record the currently hovered node, capturing its liveness token in lockstep
    // (mirrors setPressedNode/setFocusedInput). Lets liveHoveredNode() detect a
    // hovered node freed outside onNodeRemoved (e.g. a reconcile with no
    // node-removed callback wired) instead of dereferencing dangling memory.
    void setHoveredNode(Node* node) {
        hoveredNode_ = node;
        hoveredNodeAlive_ = node ? node->alive : std::weak_ptr<bool>{};
    }

    // Return hoveredNode_ only if its node is still alive; otherwise clear the
    // stale pointer and return nullptr (mirrors livePressedNode/liveFocusedInput).
    Node* liveHoveredNode() const {
        if (!hoveredNode_)
            return nullptr;
        auto alive = hoveredNodeAlive_.lock();
        if (!alive || !*alive) {
            hoveredNode_ = nullptr;
            hoveredNodeAlive_.reset();
            return nullptr;
        }
        return hoveredNode_;
    }

    // Set the visual-state latch. Called ONLY on actual hover/press/focus
    // transitions (never on a non-transition mouse move); consumed by
    // consumeVisualStateChanged.
    void markVisualStateChanged() noexcept { visualStateChanged_ = true; }

    // Which keyboard handler a routing search is looking for.
    enum class KeyPhase { Down, Up };

    // Does `node` carry the handler matching `phase`?
    static bool hasKeyHandler(Node* node, KeyPhase phase);

    // Find the first pre-order node carrying the handler for `phase` (DFS).
    // `depth` bounds the descent against maxTreeDepth_ (see hitTest): an over-deep
    // subtree stops being searched and emits one diagnostic instead of overflowing
    // the stack.
    Node* findKeyTarget(Node* node, KeyPhase phase, int depth = 0);

    // Route a caught user-callback exception to the installed sink (no-op if none
    // installed; the Host always installs one that applies the default policy).
    void reportError(std::string_view where, const std::exception* eOrNull) noexcept {
        if (!errorHandler_)
            return;
        try {
            errorHandler_(where, eOrNull);
        } catch (...) {
        }
    }

    ErrorHandler errorHandler_;
    // Depth ceiling for the recursive event-path walks. kMaxTreeDepth in
    // production; lowered by tests via setMaxTreeDepth to drive the guard with a
    // shallow tree.
    int maxTreeDepth_ = kMaxTreeDepth;
    // mutable: liveHoveredNode() lazily clears a stale hover from const
    // getHoveredNode(). hoveredNodeAlive_ observes the hovered node's liveness
    // token (Node::alive) without owning it.
    mutable Node* hoveredNode_ = nullptr;
    mutable std::weak_ptr<bool> hoveredNodeAlive_;
    // mutable: liveFocusedInput() lazily clears a stale focus from const
    // getFocusedInput(). focusedInputAlive_ observes the focused node's
    // liveness token (Node::alive) without owning it.
    mutable InputNode* focusedInput_ = nullptr;
    mutable std::weak_ptr<bool> focusedInputAlive_;
    // The node that received the last mouse press (+ its button), for click
    // press/release matching. mutable: livePressedNode() lazily clears a stale
    // press; pressedNodeAlive_ observes the node's liveness token without owning.
    mutable Node* pressedNode_ = nullptr;
    mutable std::weak_ptr<bool> pressedNodeAlive_;
    MouseButton pressedButton_ = MouseButton::Left;
    // Visual-state latch: a hover/press/focus transition occurred since the
    // last consumeVisualStateChanged (see markVisualStateChanged).
    bool visualStateChanged_ = false;
};

}  // namespace yui
