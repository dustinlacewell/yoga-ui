#pragma once

#include "EditCommand.hpp"
#include "ErrorHandler.hpp"
#include "Event.hpp"
#include "Node.hpp"

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace yui {

// Platform clipboard seam — defined when Cut/Copy/Paste land (C5). Accepted by
// handleEditCommand already so its signature is stable across the editing
// commits.
class IClipboard;

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
    //
    // `mods` is the KeyMod bitmask held at the press (matches handleKeyDown's
    // keyMod). It rides on the dispatched Event (Event::keyMod) and is what
    // shift+click selection extension reads (C3).
    bool handleMouseDown(Node* root, float x, float y, MouseButton button = MouseButton::Left,
                         uint16_t mods = KeyMod_None) noexcept;
    bool handleMouseUp(Node* root, float x, float y, MouseButton button = MouseButton::Left) noexcept;
    bool handleMouseMove(Node* root, float x, float y) noexcept;
    bool handleScroll(Node* root, float x, float y, float deltaX, float deltaY) noexcept;

    // Keyboard events - dispatched to focused node (or root if none)
    bool handleKeyDown(Node* root, int keyCode, uint16_t keyMod, bool repeat = false) noexcept;
    bool handleKeyUp(Node* root, int keyCode, uint16_t keyMod) noexcept;

    // Text input for the focused Input — replaces the selection if any, else
    // inserts at the caret.
    void handleTextInput(const std::string& text) noexcept;
    void handleSubmit() noexcept;

    // Apply an editing command to the focused Input. Returns true iff a focused
    // Input consumed it (no focused Input -> false, so platform shims can route
    // the key elsewhere). `extend` (Shift-held) makes Move* commands move only
    // the caret, leaving the selection anchor to span the selection;
    // `clipboard` is accepted now but unused until Cut/Copy/Paste land (C5).
    bool handleEditCommand(EditCommand cmd, bool extend = false, IClipboard* clipboard = nullptr) noexcept;

    // Get the currently hovered node (for cursor changes, etc.). Validates the
    // liveness token first (mirrors getFocusedNode): a hovered node freed by a
    // reconciliation is reported as no-hover rather than a dangling pointer.
    Node* getHoveredNode() const { return liveHoveredNode(); }

    // True while a live press is being tracked — i.e. the press target holds
    // implicit pointer capture (validated against its liveness token first).
    bool hasCapture() const { return livePressedNode() != nullptr; }

    // Resolve the cursor shape for the current pointer state: walk captor→root
    // during capture (the captor owns the pointer), else hovered→root. The first
    // explicit EventProps::cursor wins, an Input in the chain defaults to IBeam,
    // fallback Arrow. Pull query — the platform polls it each frame.
    CursorShape getCursor() const;

    // Advance the multi-click clock by dt SECONDS (the same dt Host::update
    // feeds the animation walk). Presses chain into double-clicks against this
    // accumulated clock, so click timing follows the host's frame clock and
    // stays deterministic under tests — no wall clock.
    void advanceClock(float dt) noexcept { clockMs_ += static_cast<double>(dt) * 1000.0; }

    // Get the focused node, of any primitive type. Validates the liveness token
    // first: if the focused node was freed by a reconciliation, this clears the
    // stale pointer and returns nullptr so a caller never receives a dangling
    // pointer.
    Node* getFocusedNode() const { return liveFocusedNode(); }

    // Get the focused node iff it is an Input (for text input routing). The
    // typed view over getFocusedNode: a focused non-Input reads as "no focused
    // input", so the text-editing entry points below no-op for it.
    InputNode* getFocusedInput() const {
        return liveFocusedInput();
    }

    // Move focus to `node` (nullptr = blur). Programmatic focus accepts ANY
    // node — the focusable predicate gates only click/Tab ACQUISITION. Fires
    // onFocus(false) on the old node before onFocus(true) on the new; an Input
    // gaining focus restarts its caret blink.
    void focusNode(Node* node);

    // Move focus to the next/previous focusable node in document (pre-order)
    // order, wrapping at the ends. With nothing focused, Tab targets the first
    // focusable and Shift-Tab the last. While a focus trap is set (and its root
    // alive), traversal is scoped to the trap root's subtree.
    void focusNext(Node* root);
    void focusPrev(Node* root);

    // Scope Tab traversal to `node`'s subtree. The slot follows the same
    // liveness-token pattern as the focus/hover/press slots: a trap root freed
    // by a reconciliation reads as no-trap and traversal falls back to the full
    // tree.
    void setFocusTrap(Node* node) {
        trapNode_ = node;
        trapAlive_ = node ? node->alive : std::weak_ptr<bool>{};
    }
    void clearFocusTrap() { setFocusTrap(nullptr); }

    // Read-and-clear the visual-state latch: true iff a hover, press, focus, or
    // text-edit TRANSITION occurred since the last consume. Latched (not returned per
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
        if (focusedNode_ == node) {
            setFocusedNode(nullptr);
        }
        if (pressedNode_ == node) {
            setPressedNode(nullptr);
        }
        if (trapNode_ == node) {
            setFocusTrap(nullptr);
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

    // One captured mouse move: MouseMove to the captor's chain, then the drag
    // threshold machine (see the definition for the UAF reasoning on holding
    // `captor` across the two dispatches).
    bool dispatchCapturedMove(Node* captor, float x, float y);

    // Start a scrollbar gesture if the press landed on a Scroll node's overlay
    // scrollbar. Scrollbars are chrome: the press captures the scroll node
    // (pressedNode_) and is handled entirely here — a thumb press arms the
    // drag mapping, a track press pages by one viewport — with NO user-handler
    // dispatch, focus change, or click chaining. Returns true iff consumed.
    bool beginScrollbarGesture(Node* target, float x, float y, MouseButton button);

    // One captured move of an armed thumb drag: map the press-anchored pointer
    // delta through the thumb-travel scale into the target offset, through the
    // single clamp (clampScrollOffset). Track-press gestures (no axis) ignore
    // moves.
    void dragScrollbarThumb(Node* captor, float x, float y);

    // Advance the multi-click machine at a press: a press within the interval /
    // radius of the previous click (same button) extends the chain, anything
    // else restarts clickCount_ at 1.
    void updateClickChain(float x, float y, MouseButton button);

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

    // Set the focused node and capture its liveness token in lockstep. The
    // sole writer of focusedNode_ — keeps the raw pointer and the weak token
    // observing the same node so a later validate can detect a freed node.
    void setFocusedNode(Node* node) {
        focusedNode_ = node;
        focusedNodeAlive_ = node ? node->alive : std::weak_ptr<bool>{};
    }

    // Return focusedNode_ only if its node is still alive; otherwise clear the
    // stale pointer and return nullptr. Every deref of the focused node routes
    // through here so a reconciliation that freed the node (without an
    // onNodeRemoved for it) can never produce a use-after-free.
    Node* liveFocusedNode() const {
        if (!focusedNode_)
            return nullptr;
        auto alive = focusedNodeAlive_.lock();
        if (!alive || !*alive) {
            focusedNode_ = nullptr;
            focusedNodeAlive_.reset();
            return nullptr;
        }
        return focusedNode_;
    }

    // Typed view of the focused slot: the focused node iff it is an Input.
    InputNode* liveFocusedInput() const {
        Node* n = liveFocusedNode();
        return (n && n->type() == PrimitiveType::Input) ? static_cast<InputNode*>(n) : nullptr;
    }

    // Return trapNode_ only if it is still alive; otherwise clear the stale
    // pointer and return nullptr (mirrors liveFocusedNode — a removed trap root
    // silently unscopes traversal back to the full tree).
    Node* liveTrapRoot() const {
        if (!trapNode_)
            return nullptr;
        auto alive = trapAlive_.lock();
        if (!alive || !*alive) {
            trapNode_ = nullptr;
            trapAlive_.reset();
            return nullptr;
        }
        return trapNode_;
    }

    // Record the node that received the most recent mouse press, with its button
    // and press coordinates, capturing the liveness token in lockstep (mirrors
    // setFocusedInput). The MouseUp dispatch matches a click against this so a
    // release with no matching press on the same node fires nothing. Sole writer
    // of the drag anchor: a press re-anchors pressX_/pressY_ and rearms the
    // threshold machine (dragging_ = false); clearing (release / node removal)
    // disarms it. A second press while captured simply overwrites — re-targeting
    // capture and re-anchoring the drag (the single-capture-slot policy).
    void setPressedNode(Node* node, MouseButton button = MouseButton::Left, float x = 0,
                        float y = 0) {
        pressedNode_ = node;
        pressedButton_ = button;
        pressedNodeAlive_ = node ? node->alive : std::weak_ptr<bool>{};
        pressX_ = lastX_ = x;
        pressY_ = lastY_ = y;
        dragging_ = false;
        // Every press lifecycle change disarms the scrollbar gesture;
        // beginScrollbarGesture re-arms it right after recording the press.
        scrollbarGesture_ = false;
        scrollbarDragAxis_.reset();
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

    // Set the visual-state latch. Called ONLY on actual hover/press/focus/text-edit
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

    // Append every focusable node under `node` in FORWARD pre-order (document
    // order). `depth` bounds the descent against maxTreeDepth_ (see hitTest).
    void collectFocusables(Node* node, std::vector<Node*>& out, int depth = 0);

    // Shared body of focusNext/focusPrev: collect the focusable order (scoped to
    // a live trap root), step from the focused node with wraparound, focus.
    void moveFocus(Node* root, bool forward);

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
    // mutable: liveFocusedNode() lazily clears a stale focus from const
    // getFocusedNode(). focusedNodeAlive_ observes the focused node's
    // liveness token (Node::alive) without owning it.
    mutable Node* focusedNode_ = nullptr;
    mutable std::weak_ptr<bool> focusedNodeAlive_;
    // Focus-trap root: while set and alive, focusNext/focusPrev traverse only
    // its subtree. mutable: liveTrapRoot() lazily clears a stale trap.
    mutable Node* trapNode_ = nullptr;
    mutable std::weak_ptr<bool> trapAlive_;
    // The node that received the last mouse press (+ its button), for click
    // press/release matching — AND the implicit pointer-capture slot: while set,
    // mouse moves and the release route to this node regardless of what is under
    // the pointer (see handleMouseMove/handleMouseUp). mutable: livePressedNode()
    // lazily clears a stale press; pressedNodeAlive_ observes the node's liveness
    // token without owning.
    mutable Node* pressedNode_ = nullptr;
    mutable std::weak_ptr<bool> pressedNodeAlive_;
    MouseButton pressedButton_ = MouseButton::Left;
    // Drag threshold machine, anchored by setPressedNode (its sole writer for
    // the anchor/reset lifecycle). pressX_/pressY_ anchor the press;
    // lastX_/lastY_ track the previous captured-move position for per-move
    // deltas; dragging_ latches in dispatchCapturedMove once the pointer leaves
    // the kDragThresholdPx ring and suppresses the click on release.
    float pressX_ = 0;
    float pressY_ = 0;
    float lastX_ = 0;
    float lastY_ = 0;
    bool dragging_ = false;
    // Scrollbar gesture riding the capture slot: while the recorded press
    // landed on a scroll node's scrollbar, moves and the release are consumed
    // as chrome (no user-handler dispatch). scrollbarDragAxis_ is set for a
    // thumb press (the drag maps pointer deltas to that axis's offset) and
    // empty for a track press. Armed only by beginScrollbarGesture; disarmed
    // by setPressedNode (see there).
    bool scrollbarGesture_ = false;
    std::optional<ScrollAxis> scrollbarDragAxis_;
    float scrollbarDragStartTarget_ = 0;
    // Multi-click machine: the dt-accumulated clock (advanceClock) plus the last
    // press's time/pos/button, chained into clickCount_ by updateClickChain. A
    // drag resets clickCount_ to 0 so the next press starts a fresh chain.
    double clockMs_ = 0;
    double lastClickTimeMs_ = 0;
    float lastClickX_ = 0;
    float lastClickY_ = 0;
    MouseButton lastClickButton_ = MouseButton::Left;
    int clickCount_ = 0;
    // Visual-state latch: a hover/press/focus/text-edit transition occurred
    // since the last consumeVisualStateChanged (see markVisualStateChanged).
    bool visualStateChanged_ = false;
};

}  // namespace yui
