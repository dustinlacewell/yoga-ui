#include <yui/core/EventHandler.hpp>

#include <exception>

namespace yui {

namespace {

// Invoke a user callback inside its own try, routing any throw to the sink.
// Returns true iff the callback ran to completion. For hover/focus the state
// mutation has already committed (the caller structures it so) so an isolated
// throw never desyncs hoveredNode_/focusedInput_ from the node flags. For event
// dispatch the return value decides consumption: a throwing handler does NOT
// consume the event, so the bubble walk continues to ancestor handlers.
template <typename Invoke, typename Report>
bool fireCallback(std::string_view where, Invoke&& invoke, Report&& report) {
    try {
        invoke();
        return true;
    } catch (const std::exception& e) {
        report(where, &e);
    } catch (...) {
        report(where, nullptr);
    }
    return false;
}

}  // namespace

bool EventHandler::handleMouseDown(Node* root, float x, float y, MouseButton button) noexcept {
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

bool EventHandler::handleMouseUp(Node* root, float x, float y, MouseButton button) noexcept {
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

bool EventHandler::handleMouseMove(Node* root, float x, float y) noexcept {
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

bool EventHandler::handleScroll(Node* root, float x, float y, float deltaX, float deltaY) noexcept {
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

bool EventHandler::handleKeyDown(Node* root, int keyCode, uint16_t keyMod, bool repeat) noexcept {
    // Focused Input wins; otherwise route to the first pre-order onKeyDown handler.
    // liveFocusedInput() validates the liveness token so a reconciliation that
    // freed the focused input routes to the tree instead of a dangling pointer.
    Node* target;
    if (InputNode* focused = liveFocusedInput()) {
        target = static_cast<Node*>(focused);
    } else {
        target = findKeyTarget(root, KeyPhase::Down);
        if (!target) target = root;
    }
    if (!target)
        return false;

    Event event;
    event.type = Event::Type::KeyDown;
    event.keyCode = keyCode;
    event.keyMod = keyMod;
    event.keyRepeat = repeat;

    return dispatchEvent(target, event);
}

bool EventHandler::handleKeyUp(Node* root, int keyCode, uint16_t keyMod) noexcept {
    // Focused Input wins; otherwise route to the first pre-order onKeyUp handler.
    // liveFocusedInput() validates the liveness token (see handleKeyDown).
    Node* target;
    if (InputNode* focused = liveFocusedInput()) {
        target = static_cast<Node*>(focused);
    } else {
        target = findKeyTarget(root, KeyPhase::Up);
        if (!target) target = root;
    }
    if (!target)
        return false;

    Event event;
    event.type = Event::Type::KeyUp;
    event.keyCode = keyCode;
    event.keyMod = keyMod;

    return dispatchEvent(target, event);
}

Node* EventHandler::hitTest(Node* node, float x, float y, float offsetX, float offsetY, int depth) {
    // Depth guard: a pathologically deep tree would otherwise overflow the native
    // stack here. Stop descending (treat as no deeper hit) and diagnose once. The
    // caller still gets a valid hit for everything down to maxTreeDepth_.
    if (depth >= maxTreeDepth_) {
        reportError("hitTest: max tree depth exceeded — tree too deep to hit-test fully", nullptr);
        return nullptr;
    }

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
        Node* hit = hitTest(it->get(), x, y, childOffsetX, childOffsetY, depth + 1);
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

namespace {

// Select the click handler for a given mouse button from a node's props.
const std::function<void()>* clickHandlerFor(const EventProps& props, MouseButton button) {
    switch (button) {
    case MouseButton::Left:   return props.onClick ? &props.onClick : nullptr;
    case MouseButton::Right:  return props.onRightClick ? &props.onRightClick : nullptr;
    case MouseButton::Middle: return props.onMiddleClick ? &props.onMiddleClick : nullptr;
    }
    return nullptr;
}

// Click handler for the node's button, dispatched on the node's primitive type.
const std::function<void()>* clickHandlerFor(Node* node, MouseButton button) {
    switch (node->type()) {
    case PrimitiveType::Box:    return clickHandlerFor(static_cast<BoxNode*>(node)->props, button);
    case PrimitiveType::Text:   return clickHandlerFor(static_cast<TextNode*>(node)->props, button);
    case PrimitiveType::Input:  return clickHandlerFor(static_cast<InputNode*>(node)->props, button);
    case PrimitiveType::Scroll: return clickHandlerFor(static_cast<ScrollNode*>(node)->props, button);
    case PrimitiveType::Canvas: return clickHandlerFor(static_cast<CanvasNode*>(node)->props, button);
    }
    return nullptr;
}

}  // namespace

bool EventHandler::hasClickHandler(Node* root, float x, float y, MouseButton button) {
    Node* target = hitTest(root, x, y);

    // Walk up from target to root checking for handlers
    while (target) {
        const auto* handler = clickHandlerFor(target, button);
        if (handler && *handler) {
            return true;
        }
        target = target->parent;
    }

    return false;
}

bool EventHandler::dispatchEvent(Node* node, Event& event, int depth) {
    if (!node)
        return false;

    // Depth guard on the bubble walk: an over-deep ancestor chain would otherwise
    // overflow the native stack. Stop bubbling (treat as not-consumed-further) and
    // diagnose once. Handlers up to maxTreeDepth_ levels still fire normally.
    if (depth >= maxTreeDepth_) {
        reportError("dispatchEvent: max tree depth exceeded — event bubbling truncated", nullptr);
        return event.consumed;
    }

    // Get event handlers from props based on node type
    std::function<void()>* onClick = nullptr;
    std::function<void()>* onRightClick = nullptr;
    std::function<void()>* onMiddleClick = nullptr;
    std::function<void()>* onMouseDown = nullptr;
    std::function<void(float, float)>* onScroll = nullptr;
    std::function<void(int, uint16_t)>* onKeyDown = nullptr;
    std::function<void(int, uint16_t)>* onKeyUp = nullptr;

#define YUI_EXTRACT_EVENTS(nodeType, castType) \
    case PrimitiveType::nodeType: { \
        auto* n = static_cast<castType*>(node); \
        onClick = n->props.onClick ? &n->props.onClick : nullptr; \
        onRightClick = n->props.onRightClick ? &n->props.onRightClick : nullptr; \
        onMiddleClick = n->props.onMiddleClick ? &n->props.onMiddleClick : nullptr; \
        onMouseDown = n->props.onMouseDown ? &n->props.onMouseDown : nullptr; \
        onScroll = n->props.onScroll ? &n->props.onScroll : nullptr; \
        onKeyDown = n->props.onKeyDown ? &n->props.onKeyDown : nullptr; \
        onKeyUp = n->props.onKeyUp ? &n->props.onKeyUp : nullptr; \
        break; \
    }

    switch (node->type()) {
    YUI_EXTRACT_EVENTS(Box, BoxNode)
    YUI_EXTRACT_EVENTS(Text, TextNode)
    YUI_EXTRACT_EVENTS(Input, InputNode)
    YUI_EXTRACT_EVENTS(Scroll, ScrollNode)
    YUI_EXTRACT_EVENTS(Canvas, CanvasNode)
    }

#undef YUI_EXTRACT_EVENTS

    auto report = [this](std::string_view w, const std::exception* e) { reportError(w, e); };

    // The user callback is isolated; it consumes the event only if it ran to
    // completion. A throwing handler does NOT consume, so the event bubbles on to
    // ancestor handlers — a throw never aborts the rest of the dispatch walk.
    if (event.type == Event::Type::MouseDown) {
        if (event.button == MouseButton::Left && onMouseDown && *onMouseDown) {
            if (fireCallback("onMouseDown", [&] { (*onMouseDown)(); }, report))
                event.consume();
        }
    }

    // Handle click events (mouseUp triggers click)
    if (event.type == Event::Type::MouseUp) {
        if (event.button == MouseButton::Left && onClick && *onClick) {
            if (fireCallback("onClick", [&] { (*onClick)(); }, report))
                event.consume();
        } else if (event.button == MouseButton::Right && onRightClick && *onRightClick) {
            if (fireCallback("onRightClick", [&] { (*onRightClick)(); }, report))
                event.consume();
        } else if (event.button == MouseButton::Middle && onMiddleClick && *onMiddleClick) {
            if (fireCallback("onMiddleClick", [&] { (*onMiddleClick)(); }, report))
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
            if (fireCallback("onScroll", [&] { (*onScroll)(event.scrollDeltaX, event.scrollDeltaY); }, report))
                event.consume();
        }
    }

    // Handle keyboard events
    if (event.type == Event::Type::KeyDown && onKeyDown && *onKeyDown) {
        if (fireCallback("onKeyDown", [&] { (*onKeyDown)(event.keyCode, event.keyMod); }, report))
            event.consume();
    }
    if (event.type == Event::Type::KeyUp && onKeyUp && *onKeyUp) {
        if (fireCallback("onKeyUp", [&] { (*onKeyUp)(event.keyCode, event.keyMod); }, report))
            event.consume();
    }

    // If not consumed, bubble to parent
    if (!event.consumed && node->parent) {
        return dispatchEvent(node->parent, event, depth + 1);
    }

    return event.consumed;
}

void EventHandler::updateHover(Node* newHovered) {
    if (newHovered == hoveredNode_)
        return;

    auto report = [this](std::string_view w, const std::exception* e) { reportError(w, e); };

    // Clear hovered flag and call onHover(false) on old node and ancestors. The
    // flag toggle commits before the callback fires, and hoveredNode_ is assigned
    // unconditionally at the end — so a throwing onHover can never leave the node
    // flags out of sync with hoveredNode_.
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
            fireCallback("onHover", [&] { (*onHover)(false); }, report);
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
            fireCallback("onHover", [&] { (*onHover)(true); }, report);
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

    // Validate first: drops a stale focusedInput_ so the equality test below
    // can't be fooled by a freed pointer aliasing a freshly-allocated node.
    if (newFocus == liveFocusedInput())
        return;

    auto report = [this](std::string_view w, const std::exception* e) { reportError(w, e); };

    // Clear focused flag and fire onFocus(false) on old. The flag commits before
    // the callback, the callback is isolated, and focusedInput_ is reassigned
    // unconditionally — so a throwing onFocus cannot desync the flag from
    // focusedInput_.
    if (focusedInput_) {
        focusedInput_->focused = false;
        if (focusedInput_->props.onFocus) {
            InputNode* old = focusedInput_;
            fireCallback("onFocus", [&] { old->props.onFocus(false); }, report);
        }
    }

    setFocusedInput(newFocus);

    // Set focused flag and fire onFocus(true) on new
    if (focusedInput_) {
        focusedInput_->focused = true;
        if (focusedInput_->props.onFocus) {
            fireCallback("onFocus", [&] { focusedInput_->props.onFocus(true); }, report);
        }
    }
}

void EventHandler::focusInput(InputNode* node) {
    // Validate first (see updateFocus): a stale focusedInput_ must not alias
    // the incoming node and short-circuit a legitimate focus change.
    if (node == liveFocusedInput())
        return;

    auto report = [this](std::string_view w, const std::exception* e) { reportError(w, e); };

    // Unfocus old (same state-commits-before-callback discipline as updateFocus).
    if (focusedInput_) {
        focusedInput_->focused = false;
        if (focusedInput_->props.onFocus) {
            InputNode* old = focusedInput_;
            fireCallback("onFocus", [&] { old->props.onFocus(false); }, report);
        }
    }

    setFocusedInput(node);

    // Focus new
    if (focusedInput_) {
        focusedInput_->focused = true;
        if (focusedInput_->props.onFocus) {
            fireCallback("onFocus", [&] { focusedInput_->props.onFocus(true); }, report);
        }
    }
}

void EventHandler::handleTextInput(const std::string& text) noexcept {
    // Validate the focused input's liveness token before any deref: a
    // reconciliation may have freed it without an onNodeRemoved for this node.
    InputNode* focused = liveFocusedInput();
    if (!focused)
        return;

    // Optimistic advance for immediate feedback. The display state commits first;
    // only the onChange notification is isolated, so a throwing handler preserves
    // the on-screen feedback (per the decided contract) instead of rolling it back.
    focused->displayText += text;

    if (focused->props.onChange) {
        fireCallback(
            "onChange", [&] { focused->props.onChange(focused->displayText); },
            [this](std::string_view w, const std::exception* e) { reportError(w, e); });
    }
}

void EventHandler::handleBackspace() noexcept {
    InputNode* focused = liveFocusedInput();
    if (!focused)
        return;

    if (!focused->displayText.empty()) {
        // Same optimistic-advance contract as handleTextInput: commit the edit,
        // isolate only the onChange notification.
        focused->displayText.pop_back();

        if (focused->props.onChange) {
            fireCallback(
                "onChange", [&] { focused->props.onChange(focused->displayText); },
                [this](std::string_view w, const std::exception* e) { reportError(w, e); });
        }
    }
}

void EventHandler::handleSubmit() noexcept {
    InputNode* focused = liveFocusedInput();
    if (!focused)
        return;

    if (focused->props.onSubmit) {
        fireCallback(
            "onSubmit", [&] { focused->props.onSubmit(); },
            [this](std::string_view w, const std::exception* e) { reportError(w, e); });
    }
}

// Does `node` carry the handler matching the requested key phase?
// (KeyDown searches onKeyDown; KeyUp searches onKeyUp — so a node that registers
// only one of the two is reachable for the matching event and ignored for the other.)
bool EventHandler::hasKeyHandler(Node* node, KeyPhase phase) {
    auto pick = [phase](const EventProps& p) -> bool {
        return phase == KeyPhase::Down ? !!p.onKeyDown : !!p.onKeyUp;
    };
    switch (node->type()) {
    case PrimitiveType::Box:
        return pick(static_cast<BoxNode*>(node)->props);
    case PrimitiveType::Text:
        return pick(static_cast<TextNode*>(node)->props);
    case PrimitiveType::Input:
        return pick(static_cast<InputNode*>(node)->props);
    case PrimitiveType::Scroll:
        return pick(static_cast<ScrollNode*>(node)->props);
    case PrimitiveType::Canvas:
        return pick(static_cast<CanvasNode*>(node)->props);
    default:
        return false;
    }
}

// Keyboard routing contract: keyboard events go to the focused Input if any;
// otherwise to the first pre-order node handling that event type. The phase
// parameter keeps KeyDown and KeyUp coherent — a node registering both handlers
// is selected for both events, while a node registering only one is still
// reachable for that event (previously onKeyUp-only nodes were never targeted,
// because targeting always inspected onKeyDown).
Node* EventHandler::findKeyTarget(Node* node, KeyPhase phase, int depth) {
    if (!node)
        return nullptr;

    // Depth guard on the DFS: an over-deep subtree would otherwise overflow the
    // native stack. Stop searching this subtree (no target found here) and diagnose
    // once. Everything down to maxTreeDepth_ is still searched.
    if (depth >= maxTreeDepth_) {
        reportError("findKeyTarget: max tree depth exceeded — key-target search truncated", nullptr);
        return nullptr;
    }

    if (hasKeyHandler(node, phase))
        return node;

    // Search children
    for (auto& child : node->children) {
        Node* found = findKeyTarget(child.get(), phase, depth + 1);
        if (found)
            return found;
    }

    return nullptr;
}

}  // namespace yui
