#include <yui/core/EventHandler.hpp>

#include <exception>
#include <unordered_set>

namespace yui {

namespace {

// Is `ancestor` the same node as `node` or one of its ancestors? Used by click
// press/release matching: a click fires on a handler node when the press leaf is
// within that node's subtree (so press-on-child + handler-on-parent still clicks),
// bounded by the same depth guard the bubble walk uses.
bool isAncestorOrSelf(const Node* ancestor, const Node* node, int maxDepth) {
    int depth = 0;
    for (const Node* n = node; n && depth < maxDepth; n = n->parent, ++depth) {
        if (n == ancestor)
            return true;
    }
    return false;
}

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

    // Remember the press target (+ button) so the matching release fires a click
    // only on the node that ALSO received the press. Recorded even when target is
    // null so a press on empty space can't leave a stale prior press to match a
    // later release. A change of pressed node is a visual transition.
    if (target != livePressedNode())
        markVisualStateChanged();
    setPressedNode(target, button);

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

    // The press this release must match (null if the pressed node was reconciled
    // away or the button differs). Read once, then clear: a press pairs with at
    // most one release, and a stale press must not match a later unrelated one.
    // Clearing a recorded press is a visual transition (pressed -> released).
    if (livePressedNode())
        markVisualStateChanged();
    Node* pressed = (livePressedNode() && pressedButton_ == button) ? pressedNode_ : nullptr;
    setPressedNode(nullptr);

    if (!target)
        return false;

    Event event;
    event.type = Event::Type::MouseUp;
    event.x = x;
    event.y = y;
    event.button = button;
    event.pressedTarget = pressed;

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

// The onHover handler for a node, dispatched on its primitive type (mirrors
// clickHandlerFor). Returns nullptr if the node carries no onHover.
const std::function<void(bool)>* hoverHandlerFor(Node* node) {
    switch (node->type()) {
    case PrimitiveType::Box:    return static_cast<BoxNode*>(node)->props.onHover ? &static_cast<BoxNode*>(node)->props.onHover : nullptr;
    case PrimitiveType::Text:   return static_cast<TextNode*>(node)->props.onHover ? &static_cast<TextNode*>(node)->props.onHover : nullptr;
    case PrimitiveType::Input:  return static_cast<InputNode*>(node)->props.onHover ? &static_cast<InputNode*>(node)->props.onHover : nullptr;
    case PrimitiveType::Scroll: return static_cast<ScrollNode*>(node)->props.onHover ? &static_cast<ScrollNode*>(node)->props.onHover : nullptr;
    case PrimitiveType::Canvas: return static_cast<CanvasNode*>(node)->props.onHover ? &static_cast<CanvasNode*>(node)->props.onHover : nullptr;
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
    std::function<void(int, uint16_t, bool)>* onKeyDown = nullptr;
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

    // Handle click events (mouseUp triggers click). A click fires on this
    // handler-bearing node only if the press leaf (event.pressedTarget) is within
    // this node's subtree — i.e. press and release both bubble through this node.
    // So press-on-child + handler-on-parent still clicks, an in-node click works,
    // but a release whose press landed elsewhere (an orphan release, e.g. from
    // opening an overlay under the cursor) fires nothing.
    bool pressMatches = event.pressedTarget
                        && isAncestorOrSelf(node, event.pressedTarget, maxTreeDepth_);
    if (event.type == Event::Type::MouseUp && pressMatches) {
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
        if (fireCallback("onKeyDown", [&] { (*onKeyDown)(event.keyCode, event.keyMod, event.keyRepeat); }, report))
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

Node* EventHandler::lowestCommonAncestor(Node* a, Node* b) {
    if (!a || !b)
        return nullptr;

    // Chain a-to-root into a set; walk b-to-root until the first shared node.
    std::unordered_set<Node*> ancestorsOfA;
    int depth = 0;
    for (Node* n = a; n; n = n->parent) {
        if (depth++ >= maxTreeDepth_) {
            reportError("lowestCommonAncestor: max tree depth exceeded — hover cut truncated", nullptr);
            break;
        }
        ancestorsOfA.insert(n);
    }

    depth = 0;
    for (Node* n = b; n; n = n->parent) {
        if (depth++ >= maxTreeDepth_) {
            reportError("lowestCommonAncestor: max tree depth exceeded — hover cut truncated", nullptr);
            break;
        }
        if (ancestorsOfA.count(n))
            return n;
    }

    // Disjoint chains (different roots): no shared ancestor. updateHover treats
    // this as a full leave + full enter — the original, always-safe behavior.
    return nullptr;
}

void EventHandler::updateHover(Node* newHovered) {
    Node* oldNode = liveHoveredNode();
    if (newHovered == oldNode)
        return;

    // Past the equality check: the hovered node IS changing — a visual transition.
    markVisualStateChanged();

    auto report = [this](std::string_view w, const std::exception* e) { reportError(w, e); };

    // Cut enter/leave at the lowest common ancestor: nodes shared by the old and
    // new hover paths keep their hovered state and fire no callback, so a
    // sibling-to-sibling move never spuriously re-fires onHover on shared
    // ancestors. LCA == nullptr (disjoint roots) degrades to full leave + enter.
    Node* lca = lowestCommonAncestor(oldNode, newHovered);

    // Leave: clear hovered + onHover(false) from the old node up to (exclusive)
    // the LCA. The flag toggle commits before the callback fires, and the hovered
    // node is reassigned unconditionally at the end — a throwing onHover can never
    // desync node flags from hoveredNode_.
    for (Node* n = oldNode; n && n != lca; n = n->parent) {
        n->hovered = false;
        const auto* onHover = hoverHandlerFor(n);
        if (onHover && *onHover) {
            fireCallback("onHover", [&] { (*onHover)(false); }, report);
        }
    }

    // Enter: set hovered + onHover(true) from the new node up to (exclusive) the
    // LCA.
    for (Node* n = newHovered; n && n != lca; n = n->parent) {
        n->hovered = true;
        const auto* onHover = hoverHandlerFor(n);
        if (onHover && *onHover) {
            fireCallback("onHover", [&] { (*onHover)(true); }, report);
        }
    }

    setHoveredNode(newHovered);
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

    // Past the equality check: focus IS changing — a visual transition.
    markVisualStateChanged();

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

    // Set focused flag and fire onFocus(true) on new. Restart the blink cycle
    // so the caret starts visible on focus gain.
    if (focusedInput_) {
        focusedInput_->focused = true;
        focusedInput_->resetCaretBlink();
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

    // Past the equality check: focus IS changing — a visual transition.
    markVisualStateChanged();

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

    // Focus new. Restart the blink cycle so the caret starts visible.
    if (focusedInput_) {
        focusedInput_->focused = true;
        focusedInput_->resetCaretBlink();
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
        // isolate only the onChange notification. Erase a whole UTF-8 code point,
        // not a single byte, so multi-byte characters delete cleanly and leave
        // valid UTF-8 behind.
        std::string& s = focused->displayText;
        size_t i = s.size();
        do {
            --i;
        } while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
        s.erase(i);

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
