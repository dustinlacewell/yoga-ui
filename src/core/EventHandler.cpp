#include <yui/core/EventHandler.hpp>

namespace yui {

bool EventHandler::handleMouseDown(Node* root, float x, float y, MouseButton button) {
    Node* target = hitTest(root, x, y);

    // Update focus on click
    updateFocus(target);

    if (!target)
        return false;

    Event event;
    event.type = Event::Type::MouseDown;
    event.x = x;
    event.y = y;
    event.button = button;

    return dispatchEvent(target, event);
}

bool EventHandler::handleMouseUp(Node* root, float x, float y, MouseButton button) {
    Node* target = hitTest(root, x, y);
    if (!target)
        return false;

    Event event;
    event.type = Event::Type::MouseUp;
    event.x = x;
    event.y = y;
    event.button = button;

    return dispatchEvent(target, event);
}

bool EventHandler::handleMouseMove(Node* root, float x, float y) {
    Node* target = hitTest(root, x, y);

    // Update hover state
    updateHover(target);

    if (!target)
        return false;

    Event event;
    event.type = Event::Type::MouseMove;
    event.x = x;
    event.y = y;

    return dispatchEvent(target, event);
}

bool EventHandler::handleScroll(Node* root, float x, float y, float deltaX, float deltaY) {
    Node* target = hitTest(root, x, y);
    if (!target)
        return false;

    Event event;
    event.type = Event::Type::Scroll;
    event.x = x;
    event.y = y;
    event.scrollDeltaX = deltaX;
    event.scrollDeltaY = deltaY;

    return dispatchEvent(target, event);
}

bool EventHandler::handleKeyDown(Node* root, int keyCode, uint16_t keyMod, bool repeat) {
    // Keyboard events go to focused node, or root if nothing focused
    Node* target = focusedInput_ ? static_cast<Node*>(focusedInput_) : root;
    if (!target)
        return false;

    Event event;
    event.type = Event::Type::KeyDown;
    event.keyCode = keyCode;
    event.keyMod = keyMod;
    event.keyRepeat = repeat;

    return dispatchEvent(target, event);
}

bool EventHandler::handleKeyUp(Node* root, int keyCode, uint16_t keyMod) {
    // Keyboard events go to focused node, or root if nothing focused
    Node* target = focusedInput_ ? static_cast<Node*>(focusedInput_) : root;
    if (!target)
        return false;

    Event event;
    event.type = Event::Type::KeyUp;
    event.keyCode = keyCode;
    event.keyMod = keyMod;

    return dispatchEvent(target, event);
}

Node* EventHandler::hitTest(Node* node, float x, float y, float offsetX, float offsetY) {
    if (!node)
        return nullptr;

    float nodeX = offsetX + node->layout.left;
    float nodeY = offsetY + node->layout.top;

    // Check if point is inside this node
    if (!containsPoint(node, x, y, offsetX, offsetY)) {
        return nullptr;
    }

    // Adjust child offset for scroll containers
    float childOffsetX = nodeX;
    float childOffsetY = nodeY;
    if (node->type() == PrimitiveType::Scroll) {
        auto* scrollNode = static_cast<ScrollNode*>(node);
        childOffsetX -= scrollNode->scrollOffsetX;
        childOffsetY -= scrollNode->scrollOffsetY;
    }

    // Check children in reverse order (front to back)
    for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
        Node* hit = hitTest(it->get(), x, y, childOffsetX, childOffsetY);
        if (hit)
            return hit;
    }

    // No child hit, return this node
    return node;
}

bool EventHandler::containsPoint(Node* node, float x, float y, float offsetX, float offsetY) {
    float left = offsetX + node->layout.left;
    float top = offsetY + node->layout.top;
    float right = left + node->layout.width;
    float bottom = top + node->layout.height;

    return x >= left && x < right && y >= top && y < bottom;
}

bool EventHandler::hasClickHandler(Node* root, float x, float y, MouseButton button) {
    Node* target = hitTest(root, x, y);

    // Walk up from target to root checking for handlers
    while (target) {
        std::function<void()>* handler = nullptr;

        switch (target->type()) {
        case PrimitiveType::Box: {
            auto* boxNode = static_cast<BoxNode*>(target);
            handler = (button == MouseButton::Left)
                          ? (boxNode->props.onClick ? &boxNode->props.onClick : nullptr)
                          : (boxNode->props.onRightClick ? &boxNode->props.onRightClick : nullptr);
            break;
        }
        case PrimitiveType::Text: {
            auto* textNode = static_cast<TextNode*>(target);
            handler = (button == MouseButton::Left)
                          ? (textNode->props.onClick ? &textNode->props.onClick : nullptr)
                          : (textNode->props.onRightClick ? &textNode->props.onRightClick : nullptr);
            break;
        }
        case PrimitiveType::Input: {
            auto* inputNode = static_cast<InputNode*>(target);
            handler = (button == MouseButton::Left)
                          ? (inputNode->props.onClick ? &inputNode->props.onClick : nullptr)
                          : (inputNode->props.onRightClick ? &inputNode->props.onRightClick : nullptr);
            break;
        }
        case PrimitiveType::Scroll: {
            auto* scrollNode = static_cast<ScrollNode*>(target);
            handler = (button == MouseButton::Left)
                          ? (scrollNode->props.onClick ? &scrollNode->props.onClick : nullptr)
                          : (scrollNode->props.onRightClick ? &scrollNode->props.onRightClick : nullptr);
            break;
        }
        case PrimitiveType::Canvas: {
            auto* canvasNode = static_cast<CanvasNode*>(target);
            handler = (button == MouseButton::Left)
                          ? (canvasNode->props.onClick ? &canvasNode->props.onClick : nullptr)
                          : (canvasNode->props.onRightClick ? &canvasNode->props.onRightClick : nullptr);
            break;
        }
        }

        if (handler && *handler) {
            return true;
        }

        target = target->parent;
    }

    return false;
}

bool EventHandler::dispatchEvent(Node* node, Event& event) {
    if (!node)
        return false;

    // Get event handlers from props based on node type
    std::function<void()>* onClick = nullptr;
    std::function<void()>* onRightClick = nullptr;
    std::function<void(float, float)>* onScroll = nullptr;
    std::function<void(int, uint16_t)>* onKeyDown = nullptr;
    std::function<void(int, uint16_t)>* onKeyUp = nullptr;

    switch (node->type()) {
    case PrimitiveType::Box: {
        auto* boxNode = static_cast<BoxNode*>(node);
        onClick = boxNode->props.onClick ? &boxNode->props.onClick : nullptr;
        onRightClick = boxNode->props.onRightClick ? &boxNode->props.onRightClick : nullptr;
        onScroll = boxNode->props.onScroll ? &boxNode->props.onScroll : nullptr;
        onKeyDown = boxNode->props.onKeyDown ? &boxNode->props.onKeyDown : nullptr;
        onKeyUp = boxNode->props.onKeyUp ? &boxNode->props.onKeyUp : nullptr;
        break;
    }
    case PrimitiveType::Text: {
        auto* textNode = static_cast<TextNode*>(node);
        onClick = textNode->props.onClick ? &textNode->props.onClick : nullptr;
        onRightClick = textNode->props.onRightClick ? &textNode->props.onRightClick : nullptr;
        onScroll = textNode->props.onScroll ? &textNode->props.onScroll : nullptr;
        onKeyDown = textNode->props.onKeyDown ? &textNode->props.onKeyDown : nullptr;
        onKeyUp = textNode->props.onKeyUp ? &textNode->props.onKeyUp : nullptr;
        break;
    }
    case PrimitiveType::Input: {
        auto* inputNode = static_cast<InputNode*>(node);
        onClick = inputNode->props.onClick ? &inputNode->props.onClick : nullptr;
        onRightClick = inputNode->props.onRightClick ? &inputNode->props.onRightClick : nullptr;
        onScroll = inputNode->props.onScroll ? &inputNode->props.onScroll : nullptr;
        onKeyDown = inputNode->props.onKeyDown ? &inputNode->props.onKeyDown : nullptr;
        onKeyUp = inputNode->props.onKeyUp ? &inputNode->props.onKeyUp : nullptr;
        break;
    }
    case PrimitiveType::Scroll: {
        auto* scrollNode = static_cast<ScrollNode*>(node);
        onClick = scrollNode->props.onClick ? &scrollNode->props.onClick : nullptr;
        onRightClick = scrollNode->props.onRightClick ? &scrollNode->props.onRightClick : nullptr;
        onScroll = scrollNode->props.onScroll ? &scrollNode->props.onScroll : nullptr;
        onKeyDown = scrollNode->props.onKeyDown ? &scrollNode->props.onKeyDown : nullptr;
        onKeyUp = scrollNode->props.onKeyUp ? &scrollNode->props.onKeyUp : nullptr;
        break;
    }
    case PrimitiveType::Canvas: {
        auto* canvasNode = static_cast<CanvasNode*>(node);
        onClick = canvasNode->props.onClick ? &canvasNode->props.onClick : nullptr;
        onRightClick = canvasNode->props.onRightClick ? &canvasNode->props.onRightClick : nullptr;
        onScroll = canvasNode->props.onScroll ? &canvasNode->props.onScroll : nullptr;
        onKeyDown = canvasNode->props.onKeyDown ? &canvasNode->props.onKeyDown : nullptr;
        onKeyUp = canvasNode->props.onKeyUp ? &canvasNode->props.onKeyUp : nullptr;
        break;
    }
    }

    // Handle click events (mouseUp triggers click)
    if (event.type == Event::Type::MouseUp) {
        if (event.button == MouseButton::Left && onClick && *onClick) {
            (*onClick)();
            event.consume();
        } else if (event.button == MouseButton::Right && onRightClick && *onRightClick) {
            (*onRightClick)();
            event.consume();
        }
    }

    // Handle scroll events
    if (event.type == Event::Type::Scroll) {
        // ScrollNode handles scroll automatically if content exceeds bounds
        if (node->type() == PrimitiveType::Scroll) {
            auto* scrollNode = static_cast<ScrollNode*>(node);
            scrollNode->updateContentSize();

            // Check if there's content to scroll
            bool canScrollX = scrollNode->contentWidth > scrollNode->layout.width;
            bool canScrollY = scrollNode->contentHeight > scrollNode->layout.height;

            if (canScrollY || canScrollX) {
                // Update target (smooth scrolling interpolates toward this)
                scrollNode->targetScrollX -= event.scrollDeltaX;
                scrollNode->targetScrollY -= event.scrollDeltaY;
                scrollNode->clampScrollOffset();
                event.consume();
            }
        }

        // Custom scroll handler
        if (!event.consumed && onScroll && *onScroll) {
            (*onScroll)(event.scrollDeltaX, event.scrollDeltaY);
            event.consume();
        }
    }

    // Handle keyboard events
    if (event.type == Event::Type::KeyDown && onKeyDown && *onKeyDown) {
        (*onKeyDown)(event.keyCode, event.keyMod);
        event.consume();
    }
    if (event.type == Event::Type::KeyUp && onKeyUp && *onKeyUp) {
        (*onKeyUp)(event.keyCode, event.keyMod);
        event.consume();
    }

    // If not consumed, bubble to parent
    if (!event.consumed && node->parent) {
        return dispatchEvent(node->parent, event);
    }

    return event.consumed;
}

void EventHandler::updateHover(Node* newHovered) {
    if (newHovered == hoveredNode_)
        return;

    // Clear hovered flag and call onHover(false) on old node and ancestors
    Node* oldNode = hoveredNode_;
    while (oldNode) {
        oldNode->hovered = false;

        std::function<void(bool)>* onHover = nullptr;

        switch (oldNode->type()) {
        case PrimitiveType::Box:
            onHover = static_cast<BoxNode*>(oldNode)->props.onHover ? &static_cast<BoxNode*>(oldNode)->props.onHover
                                                                    : nullptr;
            break;
        case PrimitiveType::Text:
            onHover = static_cast<TextNode*>(oldNode)->props.onHover ? &static_cast<TextNode*>(oldNode)->props.onHover
                                                                     : nullptr;
            break;
        case PrimitiveType::Input:
            onHover = static_cast<InputNode*>(oldNode)->props.onHover ? &static_cast<InputNode*>(oldNode)->props.onHover
                                                                      : nullptr;
            break;
        case PrimitiveType::Scroll:
            onHover = static_cast<ScrollNode*>(oldNode)->props.onHover
                          ? &static_cast<ScrollNode*>(oldNode)->props.onHover
                          : nullptr;
            break;
        case PrimitiveType::Canvas:
            onHover = static_cast<CanvasNode*>(oldNode)->props.onHover
                          ? &static_cast<CanvasNode*>(oldNode)->props.onHover
                          : nullptr;
            break;
        }

        if (onHover && *onHover) {
            (*onHover)(false);
        }

        oldNode = oldNode->parent;
    }

    // Set hovered flag and call onHover(true) on new node and ancestors
    Node* newNode = newHovered;
    while (newNode) {
        newNode->hovered = true;

        std::function<void(bool)>* onHover = nullptr;

        switch (newNode->type()) {
        case PrimitiveType::Box:
            onHover = static_cast<BoxNode*>(newNode)->props.onHover ? &static_cast<BoxNode*>(newNode)->props.onHover
                                                                    : nullptr;
            break;
        case PrimitiveType::Text:
            onHover = static_cast<TextNode*>(newNode)->props.onHover ? &static_cast<TextNode*>(newNode)->props.onHover
                                                                     : nullptr;
            break;
        case PrimitiveType::Input:
            onHover = static_cast<InputNode*>(newNode)->props.onHover ? &static_cast<InputNode*>(newNode)->props.onHover
                                                                      : nullptr;
            break;
        case PrimitiveType::Scroll:
            onHover = static_cast<ScrollNode*>(newNode)->props.onHover
                          ? &static_cast<ScrollNode*>(newNode)->props.onHover
                          : nullptr;
            break;
        case PrimitiveType::Canvas:
            onHover = static_cast<CanvasNode*>(newNode)->props.onHover
                          ? &static_cast<CanvasNode*>(newNode)->props.onHover
                          : nullptr;
            break;
        }

        if (onHover && *onHover) {
            (*onHover)(true);
        }

        newNode = newNode->parent;
    }

    hoveredNode_ = newHovered;
}

void EventHandler::updateFocus(Node* clicked) {
    InputNode* newFocus = nullptr;

    // Walk up from clicked node to find an input
    Node* node = clicked;
    while (node) {
        if (node->type() == PrimitiveType::Input) {
            newFocus = static_cast<InputNode*>(node);
            break;
        }
        node = node->parent;
    }

    if (newFocus == focusedInput_)
        return;

    // Clear focused flag and fire onFocus(false) on old
    if (focusedInput_) {
        focusedInput_->focused = false;
        if (focusedInput_->props.onFocus) {
            focusedInput_->props.onFocus(false);
        }
    }

    focusedInput_ = newFocus;

    // Set focused flag and fire onFocus(true) on new
    if (focusedInput_) {
        focusedInput_->focused = true;
        if (focusedInput_->props.onFocus) {
            focusedInput_->props.onFocus(true);
        }
    }
}

void EventHandler::handleTextInput(const std::string& text) {
    if (!focusedInput_ || !focusedInput_->props.value)
        return;

    *focusedInput_->props.value += text;

    if (focusedInput_->props.onChange) {
        focusedInput_->props.onChange(*focusedInput_->props.value);
    }
}

void EventHandler::handleBackspace() {
    if (!focusedInput_ || !focusedInput_->props.value)
        return;

    std::string& val = *focusedInput_->props.value;
    if (!val.empty()) {
        val.pop_back();

        if (focusedInput_->props.onChange) {
            focusedInput_->props.onChange(val);
        }
    }
}

void EventHandler::handleSubmit() {
    if (!focusedInput_)
        return;

    if (focusedInput_->props.onSubmit) {
        focusedInput_->props.onSubmit();
    }
}

}  // namespace yui
