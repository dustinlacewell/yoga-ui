#pragma once

#include "Event.hpp"
#include "Node.hpp"

namespace yui {

// Handles event dispatch and hit testing
class EventHandler {
public:
    // Process a mouse event at absolute coordinates
    // Returns true if any node consumed the event
    bool handleMouseDown(Node* root, float x, float y, MouseButton button = MouseButton::Left);
    bool handleMouseUp(Node* root, float x, float y, MouseButton button = MouseButton::Left);
    bool handleMouseMove(Node* root, float x, float y);
    bool handleScroll(Node* root, float x, float y, float deltaX, float deltaY);

    // Keyboard events - dispatched to focused node (or root if none)
    bool handleKeyDown(Node* root, int keyCode, uint16_t keyMod, bool repeat = false);
    bool handleKeyUp(Node* root, int keyCode, uint16_t keyMod);

    // Text input for focused input node
    void handleTextInput(const std::string& text);
    void handleBackspace();
    void handleSubmit();

    // Get the currently hovered node (for cursor changes, etc.)
    Node* getHoveredNode() const { return hoveredNode_; }

    // Get the focused input node (for text input routing)
    InputNode* getFocusedInput() const { return focusedInput_; }

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

    Node* hoveredNode_ = nullptr;
    InputNode* focusedInput_ = nullptr;
};

}  // namespace yui
