#pragma once

#include "ErrorHandler.hpp"
#include "Event.hpp"
#include "Node.hpp"

#include <exception>
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

    // Get the focused input node (for text input routing)
    InputNode* getFocusedInput() const { return focusedInput_; }

    // Programmatically focus an input node (used by autoFocus)
    void focusInput(InputNode* node);

    // Check if node or ancestors have a click handler for the given button
    bool hasClickHandler(Node* root, float x, float y, MouseButton button = MouseButton::Left);

    // Called when a node is being removed from the tree
    // Clears any references to this node
    void onNodeRemoved(Node* node) {
        if (hoveredNode_ == node) {
            hoveredNode_ = nullptr;
        }
        if (focusedInput_ == node) {
            focusedInput_ = nullptr;
        }
    }

    // Hit test: find deepest node containing point
    Node* hitTest(Node* node, float x, float y, float offsetX = 0, float offsetY = 0);

private:
    // Check if point is inside node bounds
    bool containsPoint(Node* node, float x, float y, float offsetX, float offsetY);

    // Dispatch event to node and ancestors (bubbling)
    bool dispatchEvent(Node* node, Event& event);

    // Fire hover callbacks
    void updateHover(Node* newHovered);

    // Update focus when clicking
    void updateFocus(Node* clicked);

    // Find first node with a keyboard handler (DFS)
    Node* findKeyTarget(Node* node);

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
    Node* hoveredNode_ = nullptr;
    InputNode* focusedInput_ = nullptr;
};

}  // namespace yui
