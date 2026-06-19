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

    // Get the currently hovered node (for cursor changes, etc.)
    Node* getHoveredNode() const { return hoveredNode_; }

    // Get the focused input node (for text input routing). Validates the
    // liveness token first: if the focused InputNode was freed by a
    // reconciliation, this clears the stale pointer and returns nullptr so a
    // caller never receives a dangling pointer.
    InputNode* getFocusedInput() const {
        return liveFocusedInput();
    }

    // Programmatically focus an input node (used by autoFocus)
    void focusInput(InputNode* node);

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
            hoveredNode_ = nullptr;
        }
        if (focusedInput_ == node) {
            setFocusedInput(nullptr);
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
    Node* hoveredNode_ = nullptr;
    // mutable: liveFocusedInput() lazily clears a stale focus from const
    // getFocusedInput(). focusedInputAlive_ observes the focused node's
    // liveness token (Node::alive) without owning it.
    mutable InputNode* focusedInput_ = nullptr;
    mutable std::weak_ptr<bool> focusedInputAlive_;
};

}  // namespace yui
