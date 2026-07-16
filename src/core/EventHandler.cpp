#include <yui/core/EventHandler.hpp>

#include <yui/core/Clipboard.hpp>
#include <yui/core/NodeRef.hpp>  // absoluteRect (scrollbar-local press coords)
#include <yui/core/RenderDefaults.hpp>
#include <yui/core/Utf8.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <unordered_set>

namespace yui {

namespace rd = render_defaults;

namespace {

// Chebyshev (max-axis) distance between two points: the square "ring" both the
// drag threshold and the multi-click radius are measured in.
float chebyshev(float ax, float ay, float bx, float by) {
    return std::max(std::fabs(ax - bx), std::fabs(ay - by));
}

// The host's text measurer, recovered from the node's per-host Yoga config
// context — the same seam TextNode::measureFunc reads. Null (⇒ the fallback
// heuristic inside InputNode's geometry) on a bare test reconciler with no
// measurer installed.
const ITextMeasurer* measurerOf(const Node* node) {
    return static_cast<const ITextMeasurer*>(YGConfigGetContext(YGNodeGetConfig(node->yogaNode)));
}

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

bool EventHandler::handleMouseDown(Node* root, float x, float y, MouseButton button,
                                   uint16_t mods) noexcept {
    Node* target = topmostHit(root, x, y);

    // A press on a scroll node's scrollbar is chrome, consumed before
    // any of the content-press machinery (focus, click chain, user dispatch).
    if (beginScrollbarGesture(target, x, y, button))
        return true;

    // Update focus on click. The pre-click focused node is read FIRST: the
    // shift+click extension below applies only to an input that was already
    // focused before this press (a focus-gaining click has no selection
    // gesture to extend).
    Node* focusedBefore = liveFocusedNode();
    updateFocus(target);

    // Chain the multi-click counter before recording the press, so the events
    // this press produces carry its position in the chain.
    updateClickChain(x, y, button);

    // Remember the press target (+ button + anchor) so the matching release fires
    // a click only on the node that ALSO received the press. Recorded even when
    // target is null so a press on empty space can't leave a stale prior press to
    // match a later release. pressedNode_ doubles as the implicit pointer-capture
    // slot (see handleMouseMove); a second press while captured overwrites it —
    // re-targeting capture and re-anchoring the drag threshold. A change of
    // pressed node is a visual transition.
    if (target != livePressedNode())
        markVisualStateChanged();
    setPressedNode(target, button, x, y);

    if (!target)
        return false;

    // Click-to-position: a press on an Input places the caret at the clicked
    // glyph boundary (the mouse analog of the arrow keys). The window x maps
    // into text space by removing the content-box origin and text pad, then
    // adding back the follow-scroll (the textX contract on
    // InputNode::indexAtPoint). A plain click collapses the anchor onto the
    // caret; SHIFT+click on the ALREADY-focused input leaves the anchor put and
    // moves only the caret (the moving end), extending the selection to the
    // clicked boundary — the same anchor-fixed/caret-moves machinery as the
    // extended arrow moves. A chained LEFT press upgrades the gesture: the
    // double-click selects the word (same-class run) under the boundary, the
    // triple selects all (single-line: the whole value IS the line). Either
    // way anchor and caret land here at press time; a drag that follows moves
    // only the caret (dispatchCapturedMove), and the release never collapses.
    if (target->type() == PrimitiveType::Input) {
        auto* input = static_cast<InputNode*>(target);
        layout::Rect abs = absoluteRect(input);
        const ITextMeasurer* m = measurerOf(input);
        float textX = x - (abs.x + input->layout.insetLeft + rd::kInputTextPad) + input->textScrollX;
        // The y maps like the x (content-box origin plus follow-scroll) and is
        // consumed only by a multiline input's line pick — multiline text is
        // TOP-aligned at the content top, no vertical text pad.
        float textY = y - (abs.y + input->layout.insetTop) + input->textScrollY;
        size_t clickIndex = input->indexAtPoint(textX, textY, m);
        input->verticalNavGoalX.reset();  // a click re-anchors the goal column
        if (button == MouseButton::Left && clickCount_ >= 3) {
            // Triple-click and beyond (chains cap at triple): select all,
            // anchor front / caret back (the moving end) — matching
            // EditCommand::SelectAll so the follow-scroll reveals the tail.
            input->selectionAnchor = 0;
            input->caret = input->displayText.size();
        } else if (button == MouseButton::Left && clickCount_ == 2) {
            // Word selection uses the wrap tokenizer's boundary classes
            // (wordRangeAt): a click on whitespace selects the space run.
            auto [wordBegin, wordEnd] = input->wordRangeAt(clickIndex);
            input->selectionAnchor = wordBegin;
            input->caret = wordEnd;
        } else {
            input->caret = clickIndex;
            if (!((mods & KeyMod_Shift) && focusedBefore == target))
                input->clearSelection();
        }
        input->resetCaretBlink();  // the placed caret shows immediately
        input->scrollCaretIntoView(m);
        markVisualStateChanged();
    }

    Event event;
    event.type = Event::Type::MouseDown;
    event.x = x;
    event.y = y;
    event.button = button;
    event.keyMod = mods;
    event.clickCount = clickCount_;

    return dispatchEvent(target, event);
}

bool EventHandler::handleMouseUp(Node* root, float x, float y, MouseButton button) noexcept {
    // The pointer may have moved off the press target: the hit result records
    // where the release LANDED (event payload, click gate, hover resync) — it is
    // never the dispatch target while a captor holds the pointer.
    Node* releaseTarget = topmostHit(root, x, y);

    // The captor this release must go to (null if the pressed node was reconciled
    // away — its liveness token died). Read once, then clear: a press pairs with
    // at most one release, and a stale press must not match a later unrelated
    // one. Clearing a recorded press is a visual transition (pressed -> released).
    Node* captor = livePressedNode();
    if (captor)
        markVisualStateChanged();
    Node* pressed = (captor && pressedButton_ == button) ? captor : nullptr;
    bool wasDragging = dragging_;
    bool wasScrollbarGesture = scrollbarGesture_;  // read before setPressedNode disarms it
    setPressedNode(nullptr);

    // Captured: the release goes to the captor regardless of where it landed
    // (even off-window, releaseTarget null). Uncaptured: to the hit node, as a
    // plain (orphan) release. A scrollbar gesture's release is chrome — it
    // ends the gesture and dispatches nothing.
    Node* dispatchTarget = wasScrollbarGesture ? nullptr : (captor ? captor : releaseTarget);

    bool consumed = wasScrollbarGesture;
    if (dispatchTarget) {
        Event event;
        event.type = Event::Type::MouseUp;
        event.x = x;
        event.y = y;
        event.button = button;
        // A gesture that became a drag ends with onMouseUp but no click:
        // withhold the press target the click gate matches against.
        event.pressedTarget = wasDragging ? nullptr : pressed;
        event.releaseTarget = releaseTarget;
        event.clickCount = clickCount_;
        consumed = dispatchEvent(dispatchTarget, event);
    }

    // Capture froze hover for the whole gesture; resync it to whatever is under
    // the pointer now that capture has ended.
    if (captor)
        updateHover(releaseTarget);

    return consumed;
}

bool EventHandler::handleMouseMove(Node* root, float x, float y) noexcept {
    // While a press is held, the press target has implicit pointer capture:
    // moves route to it regardless of what is under the pointer, and hover is
    // frozen until the release resyncs it (handleMouseUp). The captor is
    // re-derived from the liveness token on EVERY move — a captor freed by a
    // reconcile between events ends capture silently, with zero deref.
    if (Node* captor = livePressedNode())
        return dispatchCapturedMove(captor, x, y);

    Node* target = topmostHit(root, x, y);

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

// UAF safety: `captor` is held raw across the two dispatches below, which is
// safe by deferral — a handler that calls host.update() mid-dispatch is forced
// to UpdateStatus::Deferred (Host::inDispatch_), so no reconcile can free the
// captor while this frame of the walk is live; the deferred reconcile drains
// only after the top-level handle*() unwinds, and the NEXT move re-derives the
// captor from its liveness token.
bool EventHandler::dispatchCapturedMove(Node* captor, float x, float y) {
    // A scrollbar gesture owns the whole capture: moves drive the thumb (or
    // nothing, for a track press) and never reach user handlers.
    if (scrollbarGesture_) {
        dragScrollbarThumb(captor, x, y);
        lastX_ = x;
        lastY_ = y;
        return true;
    }

    Event event;
    event.type = Event::Type::MouseMove;
    event.x = x;
    event.y = y;
    bool consumed = dispatchEvent(captor, event);

    // Threshold machine: past kDragThresholdPx the press becomes a drag — the
    // release will not click, and the multi-click chain restarts from scratch.
    if (!dragging_ && chebyshev(x, y, pressX_, pressY_) >= rd::kDragThresholdPx) {
        dragging_ = true;
        clickCount_ = 0;
    }
    if (dragging_) {
        // Text drag-select on a focused Input captor. Built-in selection and a
        // user onDrag COEXIST (DOM-style): the selection updates here and the
        // Drag event still dispatches below — mirroring how the press both
        // places the caret and dispatches MouseDown. Runs only once the
        // threshold latched, so a sub-threshold wiggle stays a plain click;
        // never reached by a scrollbar gesture (early return above).
        if (captor->type() == PrimitiveType::Input && captor->focused && pressedButton_ == MouseButton::Left)
            dragSelectText(static_cast<InputNode*>(captor), x, y);

        Event drag;
        drag.type = Event::Type::Drag;
        drag.x = x;
        drag.y = y;
        drag.button = pressedButton_;
        drag.dragStartX = pressX_;
        drag.dragStartY = pressY_;
        drag.dragDeltaX = x - lastX_;
        drag.dragDeltaY = y - lastY_;
        consumed = dispatchEvent(captor, drag) || consumed;
    }
    lastX_ = x;
    lastY_ = y;
    return consumed;
}

bool EventHandler::beginScrollbarGesture(Node* target, float x, float y, MouseButton button) {
    if (!target || target->type() != PrimitiveType::Scroll || button != MouseButton::Left)
        return false;
    auto* scroll = static_cast<ScrollNode*>(target);
    layout::Rect abs = absoluteRect(scroll);
    ScrollbarPart part = scroll->scrollbarHitTest(x - abs.x, y - abs.y);
    if (part == ScrollbarPart::None)
        return false;

    // The press transition is a visual change (mirrors the content-press path);
    // the scroll motion itself repaints via the update walk's animating result.
    markVisualStateChanged();
    setPressedNode(target, button, x, y);  // capture: further moves route here
    scrollbarGesture_ = true;

    switch (part) {
    case ScrollbarPart::VerticalThumb:
        scrollbarDragAxis_ = ScrollAxis::Vertical;
        scrollbarDragStartTarget_ = scroll->targetScrollY;
        break;
    case ScrollbarPart::HorizontalThumb:
        scrollbarDragAxis_ = ScrollAxis::Horizontal;
        scrollbarDragStartTarget_ = scroll->targetScrollX;
        break;
    case ScrollbarPart::VerticalTrack: {
        // Page toward the click: one viewport up above the thumb, down below.
        float dir = (y - abs.y) < scroll->scrollbar(ScrollAxis::Vertical).thumb.y ? -1.0f : 1.0f;
        scroll->targetScrollY += dir * scroll->viewportHeight();
        scroll->clampScrollOffset();
        break;
    }
    case ScrollbarPart::HorizontalTrack: {
        float dir = (x - abs.x) < scroll->scrollbar(ScrollAxis::Horizontal).thumb.x ? -1.0f : 1.0f;
        scroll->targetScrollX += dir * scroll->viewportWidth();
        scroll->clampScrollOffset();
        break;
    }
    case ScrollbarPart::None:
        break;  // unreachable (gated above)
    }
    return true;
}

void EventHandler::dragScrollbarThumb(Node* captor, float x, float y) {
    if (!scrollbarDragAxis_ || captor->type() != PrimitiveType::Scroll)
        return;
    auto* scroll = static_cast<ScrollNode*>(captor);
    // Press-anchored absolute mapping: total pointer delta times the thumb
    // travel scale, from the target recorded at press. Clamping is left to the
    // single clamp, so dragging past an end saturates and retraces cleanly.
    float scale = scroll->scrollPerThumbPixel(*scrollbarDragAxis_);
    bool vertical = *scrollbarDragAxis_ == ScrollAxis::Vertical;
    float before = vertical ? scroll->targetScrollY : scroll->targetScrollX;
    if (vertical)
        scroll->targetScrollY = scrollbarDragStartTarget_ + (y - pressY_) * scale;
    else
        scroll->targetScrollX = scrollbarDragStartTarget_ + (x - pressX_) * scale;
    scroll->clampScrollOffset();
    // Latch the repaint here too: a sub-snap-threshold move would otherwise be
    // snapped silently by updateSmooth (offset moves, animating stays false).
    if ((vertical ? scroll->targetScrollY : scroll->targetScrollX) != before)
        markVisualStateChanged();
}

void EventHandler::dragSelectText(InputNode* input, float x, float y) {
    // The same window→text mapping as the press (see handleMouseDown), against
    // the CURRENT textScrollX/Y — as the follow-scroll chases the caret, the
    // same window point maps ever deeper into the text, so holding past a
    // content edge keeps scrolling and extending (indexAtPoint clamps at the
    // bounds). The anchor is untouched: after a plain press it is the press
    // boundary, after a shift+press the pre-press anchor, after a double-click
    // the word start — the drag always moves the caret by CHARACTER from there
    // (word-granular double-click drag is deliberately not implemented in v1).
    layout::Rect abs = absoluteRect(input);
    const ITextMeasurer* m = measurerOf(input);
    float textX = x - (abs.x + input->layout.insetLeft + rd::kInputTextPad) + input->textScrollX;
    float textY = y - (abs.y + input->layout.insetTop) + input->textScrollY;
    size_t index = input->indexAtPoint(textX, textY, m);
    if (index == input->caret)
        return;  // no boundary crossed: nothing changed, nothing to repaint
    input->caret = index;
    input->verticalNavGoalX.reset();  // a drag re-anchors the goal column
    input->resetCaretBlink();         // the moving caret shows immediately
    input->scrollCaretIntoView(m);
    markVisualStateChanged();
}

void EventHandler::updateClickChain(float x, float y, MouseButton button) {
    bool chained = (clockMs_ - lastClickTimeMs_) <= rd::kMultiClickIntervalMs &&
                   chebyshev(x, y, lastClickX_, lastClickY_) <= rd::kMultiClickRadiusPx &&
                   button == lastClickButton_;
    clickCount_ = chained ? clickCount_ + 1 : 1;
    lastClickTimeMs_ = clockMs_;
    lastClickX_ = x;
    lastClickY_ = y;
    lastClickButton_ = button;
}

bool EventHandler::handleScroll(Node* root, float x, float y, float deltaX, float deltaY) noexcept {
    Node* target = topmostHit(root, x, y);
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
    // The focused node (any type) wins; otherwise route to the first pre-order
    // onKeyDown handler. liveFocusedNode() validates the liveness token so a
    // reconciliation that freed the focused node routes to the tree instead of
    // a dangling pointer.
    Node* target;
    if (Node* focused = liveFocusedNode()) {
        target = focused;
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
    // The focused node (any type) wins; otherwise route to the first pre-order
    // onKeyUp handler. liveFocusedNode() validates the liveness token (see
    // handleKeyDown).
    Node* target;
    if (Node* focused = liveFocusedNode()) {
        target = focused;
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

    // Prune descent by the subtree AABB, not the node's own rect: children draw
    // unclipped, so one may overflow this node's rect and must stay hittable.
    // The node ITSELF is still only hit by its own rect (the fall-through below).
    const LayoutResult& l = node->layout;
    if (x < offsetX + l.subtreeLeft || x >= offsetX + l.subtreeRight || y < offsetY + l.subtreeTop ||
        y >= offsetY + l.subtreeBottom) {
        return nullptr;
    }

    float nodeX = offsetX + l.left;
    float nodeY = offsetY + l.top;
    bool inOwnRect = containsPoint(node, x, y, offsetX, offsetY);

    // A Portal contributes NOTHING to the main-tree walk: its content is
    // detached, laid out in root space, and reachable only through
    // topmostHit's portal pass. The zero-size subtree AABB (display:none)
    // already prunes it above; this gate makes the contract local and
    // explicit rather than resting on that sync invariant.
    if (node->type() == PrimitiveType::Portal)
        return nullptr;

    // Scroll content is clipped to the VIEWPORT (border box minus the
    // scroll's own insets), so a point in the padding band hits the Scroll
    // itself — never the clipped-away content — and children are hit-tested
    // from the content origin, matching where the renderer draws them.
    float childOffsetX = nodeX;
    float childOffsetY = nodeY;
    if (node->type() == PrimitiveType::Scroll) {
        // A Scroll clips, so the subtree-bounds relaxation does NOT apply to it:
        // nothing outside its own rect is visible or hittable. Its subtree
        // bounds ARE its own rect (syncLayoutFromYoga excludes scroll content),
        // so the prune above already enforces this; the explicit gate keeps the
        // clipping contract local rather than resting on the sync invariant.
        if (!inOwnRect)
            return nullptr;
        auto* scrollNode = static_cast<ScrollNode*>(node);
        // Scrollbars eat hits before content: a point on an active bar is the
        // scroll's own chrome (handleMouseDown routes it), never content.
        if (scrollNode->scrollbarHitTest(x - nodeX, y - nodeY) != ScrollbarPart::None)
            return node;
        // Content is clipped to the VIEWPORT (padded content box minus any
        // reserved gutters) — a point in the padding band or a gutter hits
        // the Scroll itself, never the clipped-away content beneath.
        bool inViewport = x >= nodeX + l.insetLeft && x < nodeX + l.insetLeft + scrollNode->viewportWidth() &&
                          y >= nodeY + l.insetTop && y < nodeY + l.insetTop + scrollNode->viewportHeight();
        if (!inViewport)
            return node;
        childOffsetX += l.insetLeft - scrollNode->scrollOffsetX;
        childOffsetY += l.insetTop - scrollNode->scrollOffsetY;
    }

    // Check children in reverse order (front to back): the last-drawn child that
    // contains the point wins, exactly as before — overflow changes which points
    // reach a child, never the priority among children that both contain a point.
    for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
        Node* hit = hitTest(it->get(), x, y, childOffsetX, childOffsetY, depth + 1);
        if (hit)
            return hit;
    }

    // No child hit: the node is hit only when the point is inside its OWN rect.
    // A point that reached here through descendant overflow alone misses this
    // node and falls back to whatever is behind it.
    return inOwnRect ? node : nullptr;
}

bool EventHandler::containsPoint(Node* node, float x, float y, float offsetX, float offsetY) {
    float left = offsetX + node->layout.left;
    float top = offsetY + node->layout.top;
    float right = left + node->layout.width;
    float bottom = top + node->layout.height;

    return x >= left && x < right && y >= top && y < bottom;
}

Node* EventHandler::topmostHit(Node* root, float x, float y) {
    std::vector<Node*> portals;
    collectPortals(root, portals);
    // collectPortals yields layer order back-to-front (paint order); hit wants
    // topmost first, so iterate in REVERSE. Within a portal, children hit
    // front-to-back exactly like the main walk (reverse child order), at
    // offset (0,0) — portal content is laid out in root space.
    for (auto pit = portals.rbegin(); pit != portals.rend(); ++pit) {
        for (auto cit = (*pit)->children.rbegin(); cit != (*pit)->children.rend(); ++cit) {
            if (Node* hit = hitTest(cit->get(), x, y))
                return hit;
        }
    }
    return hitTest(root, x, y);
}

namespace {

// The shared EventProps of a node, dispatched on its primitive type: the ONE
// per-type switch every event-prop read routes through (click/hover/focus/
// cursor/key dispatch below all read the base slice this returns).
EventProps* eventPropsOf(Node* node) {
    switch (node->type()) {
    case PrimitiveType::Box:    return &static_cast<BoxNode*>(node)->props;
    case PrimitiveType::Text:   return &static_cast<TextNode*>(node)->props;
    case PrimitiveType::Input:  return &static_cast<InputNode*>(node)->props;
    case PrimitiveType::Scroll: return &static_cast<ScrollNode*>(node)->props;
    case PrimitiveType::Canvas: return &static_cast<CanvasNode*>(node)->props;
    case PrimitiveType::Portal: return &static_cast<PortalNode*>(node)->props;
    }
    return nullptr;
}

// Select the click handler for a given mouse button from a node's props.
const std::function<void()>* clickHandlerFor(const EventProps& props, MouseButton button) {
    switch (button) {
    case MouseButton::Left:   return props.onClick ? &props.onClick : nullptr;
    case MouseButton::Right:  return props.onRightClick ? &props.onRightClick : nullptr;
    case MouseButton::Middle: return props.onMiddleClick ? &props.onMiddleClick : nullptr;
    }
    return nullptr;
}

// Click handler for the node's button.
const std::function<void()>* clickHandlerFor(Node* node, MouseButton button) {
    const EventProps* p = eventPropsOf(node);
    return p ? clickHandlerFor(*p, button) : nullptr;
}

// The onHover handler for a node; nullptr if the node carries none.
const std::function<void(bool)>* hoverHandlerFor(Node* node) {
    const EventProps* p = eventPropsOf(node);
    return (p && p->onHover) ? &p->onHover : nullptr;
}

// The onHoverDelay handler for a node; nullptr if the node carries none.
const std::function<void()>* hoverDelayHandlerFor(Node* node) {
    const EventProps* p = eventPropsOf(node);
    return (p && p->onHoverDelay) ? &p->onHoverDelay : nullptr;
}

// The node's hover-delay interval: its hoverDelayMs prop, or the shared default.
double hoverDelayMsFor(Node* node) {
    const EventProps* p = eventPropsOf(node);
    return (p && p->hoverDelayMs) ? static_cast<double>(*p->hoverDelayMs) : rd::kHoverDelayMs;
}

// The onFocus handler for a node (mirrors hoverHandlerFor); nullptr if none.
const std::function<void(bool)>* focusHandlerFor(Node* node) {
    const EventProps* p = eventPropsOf(node);
    return (p && p->onFocus) ? &p->onFocus : nullptr;
}

// The explicit cursor prop of a node; nullopt when the node requests no
// particular shape.
std::optional<CursorShape> cursorFor(Node* node) {
    const EventProps* p = eventPropsOf(node);
    return p ? p->cursor : std::nullopt;
}

// Can click/Tab move focus to this node? An Input always can; anything else
// must opt in via the .focusable() prop. Gates ACQUISITION only — programmatic
// focus (focusNode / Host::focus) accepts any node.
bool isFocusable(Node* node) {
    if (node->type() == PrimitiveType::Input)
        return true;
    const EventProps* p = eventPropsOf(node);
    return p && p->focusable;
}

}  // namespace

CursorShape EventHandler::getCursor() const {
    // During capture the captor owns the pointer, so its chain decides the shape
    // even while the pointer is over other nodes (hover is frozen anyway).
    Node* start = livePressedNode();
    if (!start)
        start = liveHoveredNode();

    // First explicit cursor prop wins walking toward the root; an Input without
    // one contributes its editing affordance. Depth-bounded like the other
    // ancestor walks (see isAncestorOrSelf).
    int depth = 0;
    for (Node* n = start; n && depth < maxTreeDepth_; n = n->parent, ++depth) {
        if (auto shape = cursorFor(n))
            return *shape;
        if (n->type() == PrimitiveType::Input)
            return CursorShape::IBeam;
    }
    return CursorShape::Arrow;
}

bool EventHandler::hasClickHandler(Node* root, float x, float y, MouseButton button) {
    Node* target = topmostHit(root, x, y);

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

    // Get event handlers from the node's shared EventProps slice
    std::function<void()>* onClick = nullptr;
    std::function<void()>* onRightClick = nullptr;
    std::function<void()>* onMiddleClick = nullptr;
    std::function<void()>* onDoubleClick = nullptr;
    std::function<void(float, float, MouseButton, uint16_t)>* onMouseDown = nullptr;
    std::function<void(float, float, MouseButton)>* onMouseUp = nullptr;
    std::function<void(float, float)>* onMouseMove = nullptr;
    std::function<void(const DragEvent&)>* onDrag = nullptr;
    std::function<void(float, float)>* onScroll = nullptr;
    std::function<void(int, uint16_t, bool)>* onKeyDown = nullptr;
    std::function<void(int, uint16_t)>* onKeyUp = nullptr;

    if (EventProps* p = eventPropsOf(node)) {
        onClick = p->onClick ? &p->onClick : nullptr;
        onRightClick = p->onRightClick ? &p->onRightClick : nullptr;
        onMiddleClick = p->onMiddleClick ? &p->onMiddleClick : nullptr;
        onDoubleClick = p->onDoubleClick ? &p->onDoubleClick : nullptr;
        onMouseDown = p->onMouseDown ? &p->onMouseDown : nullptr;
        onMouseUp = p->onMouseUp ? &p->onMouseUp : nullptr;
        onMouseMove = p->onMouseMove ? &p->onMouseMove : nullptr;
        onDrag = p->onDrag ? &p->onDrag : nullptr;
        onScroll = p->onScroll ? &p->onScroll : nullptr;
        onKeyDown = p->onKeyDown ? &p->onKeyDown : nullptr;
        onKeyUp = p->onKeyUp ? &p->onKeyUp : nullptr;
    }

    auto report = [this](std::string_view w, const std::exception* e) { reportError(w, e); };

    // The user callback is isolated; it consumes the event only if it ran to
    // completion. A throwing handler does NOT consume, so the event bubbles on to
    // ancestor handlers — a throw never aborts the rest of the dispatch walk.
    if (event.type == Event::Type::MouseDown && onMouseDown && *onMouseDown) {
        if (fireCallback("onMouseDown",
                         [&] { (*onMouseDown)(event.x, event.y, event.button, event.keyMod); }, report))
            event.consume();
    }

    if (event.type == Event::Type::MouseMove && onMouseMove && *onMouseMove) {
        if (fireCallback("onMouseMove", [&] { (*onMouseMove)(event.x, event.y); }, report))
            event.consume();
    }

    if (event.type == Event::Type::Drag && onDrag && *onDrag) {
        DragEvent drag{event.x,          event.y,          event.dragDeltaX, event.dragDeltaY,
                       event.dragStartX, event.dragStartY, event.button};
        if (fireCallback("onDrag", [&] { (*onDrag)(drag); }, report))
            event.consume();
    }

    // onMouseUp fires on every release dispatch — the captor receives it even
    // when the release landed elsewhere (or outside the window entirely); only
    // the click below is gated on where press AND release landed.
    if (event.type == Event::Type::MouseUp && onMouseUp && *onMouseUp) {
        if (fireCallback("onMouseUp",
                         [&] { (*onMouseUp)(event.x, event.y, event.button); }, report))
            event.consume();
    }

    // Handle click events (mouseUp triggers click). A click fires on this
    // handler-bearing node only if BOTH the press leaf (event.pressedTarget) and
    // the release leaf (event.releaseTarget) are within this node's subtree —
    // press and release both bubble through it. So press-on-child +
    // handler-on-parent still clicks, and a press/release split across two
    // children still clicks their shared ancestor, but a release whose press
    // landed elsewhere (an orphan release, e.g. from opening an overlay under
    // the cursor) — or a press whose release landed elsewhere — fires nothing.
    bool pressMatches = event.pressedTarget
                        && isAncestorOrSelf(node, event.pressedTarget, maxTreeDepth_);
    bool releaseMatches = event.releaseTarget
                          && isAncestorOrSelf(node, event.releaseTarget, maxTreeDepth_);
    if (event.type == Event::Type::MouseUp && pressMatches && releaseMatches) {
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
        // The second chained click also fires onDoubleClick (onClick above fires
        // on both presses of the chain; counts of 3+ ride in event.clickCount).
        if (event.button == MouseButton::Left && event.clickCount == 2 && onDoubleClick &&
            *onDoubleClick) {
            if (fireCallback("onDoubleClick", [&] { (*onDoubleClick)(); }, report))
                event.consume();
        }
    }

    // Handle scroll events
    if (event.type == Event::Type::Scroll) {
        // ScrollNode handles scroll automatically if content exceeds bounds
        if (node->type() == PrimitiveType::Scroll) {
            auto* scrollNode = static_cast<ScrollNode*>(node);
            scrollNode->updateContentSize();

            // Content scrolls when it exceeds the viewport — the same
            // region the renderer clips to and clampScrollOffset ranges over.
            bool canScrollX = scrollNode->contentWidth > scrollNode->viewportWidth();
            bool canScrollY = scrollNode->contentHeight > scrollNode->viewportHeight();

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
        // The pointer left the armed hover-delay node: disarm. The delay runs
        // only while the pointer stays inside it; a later re-enter re-arms a
        // fresh deadline.
        if (n == hoverDelayNode_)
            disarmHoverDelay();
        const auto* onHover = hoverHandlerFor(n);
        if (onHover && *onHover) {
            fireCallback("onHover", [&] { (*onHover)(false); }, report);
        }
    }

    // Enter: set hovered + onHover(true) from the new node up to (exclusive) the
    // LCA. The walk is deepest-first, so the FIRST onHoverDelay carrier found is
    // the deepest — the most specific delay target when such nodes nest.
    Node* delayTarget = nullptr;
    for (Node* n = newHovered; n && n != lca; n = n->parent) {
        n->hovered = true;
        if (!delayTarget && hoverDelayHandlerFor(n))
            delayTarget = n;
        const auto* onHover = hoverHandlerFor(n);
        if (onHover && *onHover) {
            fireCallback("onHover", [&] { (*onHover)(true); }, report);
        }
    }

    // Arm on a genuine ENTER only. A move within an already-armed node — or
    // deeper into a plain descendant of it — keeps that node above the LCA and
    // out of the enter chain, so its running deadline is never reset here.
    // Corollary: leaving a child delay-carrier UP into an already-hovered
    // ancestor delay-carrier arms NOTHING — the ancestor is above the LCA, not
    // a fresh enter — intended and unreachable by the Tooltip (one carrier).
    if (delayTarget)
        armHoverDelay(delayTarget, hoverDelayMsFor(delayTarget));

    setHoveredNode(newHovered);
}

void EventHandler::fireHoverDelayIfDue() noexcept {
    // The fired latch is the one-shot: after firing, the slot stays ARMED but
    // latched, so the pointer resting on the node doesn't re-fire every frame.
    // Disarm (and thus re-fire eligibility) comes only from leave/removal.
    if (hoverDelayFired_)
        return;
    Node* node = liveHoverDelayNode();
    if (!node || clockMs_ < hoverDelayDeadlineMs_)
        return;

    // Latch BEFORE the callback so a re-entrant clock advance can't double-fire.
    hoverDelayFired_ = true;
    const auto* onHoverDelay = hoverDelayHandlerFor(node);
    if (onHoverDelay && *onHoverDelay) {
        auto report = [this](std::string_view w, const std::exception* e) { reportError(w, e); };
        fireCallback("onHoverDelay", [&] { (*onHoverDelay)(); }, report);
    }
}

void EventHandler::updateFocus(Node* clicked) {
    // Walk up from the clicked node to the first focusable one. No focusable in
    // the chain ⇒ nullptr: a click on non-focusable space clears focus
    // (blur-on-click-away).
    Node* target = clicked;
    while (target && !isFocusable(target))
        target = target->parent;

    focusNode(target);
}

void EventHandler::focusNode(Node* node) {
    // Validate first: drops a stale focusedNode_ so the equality test below
    // can't be fooled by a freed pointer aliasing a freshly-allocated node.
    if (node == liveFocusedNode())
        return;

    // Past the equality check: focus IS changing — a visual transition.
    markVisualStateChanged();

    auto report = [this](std::string_view w, const std::exception* e) { reportError(w, e); };

    // Clear focused flag and fire onFocus(false) on old. The flag commits before
    // the callback, the callback is isolated, and focusedNode_ is reassigned
    // unconditionally — so a throwing onFocus cannot desync the flag from
    // focusedNode_.
    if (focusedNode_) {
        focusedNode_->focused = false;
        if (const auto* onFocus = focusHandlerFor(focusedNode_)) {
            fireCallback("onFocus", [&] { (*onFocus)(false); }, report);
        }
    }

    setFocusedNode(node);

    // Set focused flag and fire onFocus(true) on new. An Input restarts its
    // blink cycle so the caret starts visible on focus gain.
    if (focusedNode_) {
        focusedNode_->focused = true;
        if (focusedNode_->type() == PrimitiveType::Input)
            static_cast<InputNode*>(focusedNode_)->resetCaretBlink();
        if (const auto* onFocus = focusHandlerFor(focusedNode_)) {
            fireCallback("onFocus", [&] { (*onFocus)(true); }, report);
        }
    }
}

void EventHandler::collectFocusables(Node* node, std::vector<Node*>& out, int depth) {
    if (!node)
        return;

    // Depth guard on the DFS (see findKeyTarget): stop collecting this subtree
    // and diagnose once instead of overflowing the stack.
    if (depth >= maxTreeDepth_) {
        reportError("collectFocusables: max tree depth exceeded — focus traversal truncated",
                    nullptr);
        return;
    }

    // v1 limitation: Display::None and zero-size nodes are still collected —
    // traversal is structural, not visibility-aware.
    if (isFocusable(node))
        out.push_back(node);

    // FORWARD child order: Tab traverses document order. (hitTest walks children
    // in reverse for z-priority; traversal wants the order they were declared.)
    for (auto& child : node->children)
        collectFocusables(child.get(), out, depth + 1);
}

void EventHandler::moveFocus(Node* root, bool forward) {
    // A live trap root scopes the traversal to its subtree; a trap root freed by
    // a reconcile reads as no-trap (dead token) and falls back to the full tree.
    if (Node* trap = liveTrapRoot())
        root = trap;
    if (!root)
        return;

    // UAF safety: the collected raw pointers stay valid across focusNode below
    // by the same deferral argument as dispatchCapturedMove — a user onFocus
    // that calls host.update() mid-dispatch is forced to UpdateStatus::Deferred,
    // so no reconcile can free a collected node until this entry unwinds.
    std::vector<Node*> order;
    collectFocusables(root, order);
    if (order.empty())
        return;

    Node* current = liveFocusedNode();
    auto it = std::find(order.begin(), order.end(), current);
    Node* target;
    if (!current || it == order.end()) {
        // Nothing focused (or focused outside the scope): Tab enters at the
        // front, Shift-Tab at the back.
        target = forward ? order.front() : order.back();
    } else {
        // Step with wraparound at both ends.
        size_t i = static_cast<size_t>(it - order.begin());
        size_t n = order.size();
        target = order[forward ? (i + 1) % n : (i + n - 1) % n];
    }
    focusNode(target);
}

void EventHandler::focusNext(Node* root) {
    moveFocus(root, true);
}

void EventHandler::focusPrev(Node* root) {
    moveFocus(root, false);
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
    // Typing over a selection REPLACES it: the selected range goes, the text
    // lands at its start. Otherwise insertion is AT the caret (== append when
    // the caret sits at the end).
    if (focused->hasSelection()) {
        size_t begin = focused->selBegin();
        focused->displayText.erase(begin, focused->selEnd() - begin);
        focused->caret = begin;
    }
    focused->displayText.insert(focused->caret, text);
    focused->caret += text.size();
    focused->selectionAnchor = focused->caret;
    focused->verticalNavGoalX.reset();  // typing re-anchors the goal column
    focused->resetCaretBlink();         // typing keeps the caret visible
    // Invalidate the wrap/measure state BEFORE the follow-scroll reads the
    // (multiline) line structure; latches the relayout for multiline.
    noteTextEdited(focused);
    // Follow-scroll AFTER the insert: typing past the right edge scrolls the
    // text so the caret stays inside the content box.
    focused->scrollCaretIntoView(measurerOf(focused));

    // An edit is a visual transition: latch the repaint so an Input with no
    // onChange wired (no app-driven reconcile) still repaints. A single-line
    // input's size is prop-driven (repaint only); a multiline edit ALSO
    // latched a relayout above (noteTextEdited).
    markVisualStateChanged();

    if (focused->props.onChange) {
        fireCallback(
            "onChange", [&] { focused->props.onChange(focused->displayText); },
            [this](std::string_view w, const std::exception* e) { reportError(w, e); });
    }
}

namespace {

// What applying one EditCommand did: `handled` decides consumption (a command
// unimplemented for the input's mode is NOT consumed, so platform shims can
// route the key elsewhere); `textChanged` drives the onChange notification;
// `submit` asks the caller to fire onSubmit (single-line Enter).
struct EditOutcome {
    bool handled = false;
    bool textChanged = false;
    bool submit = false;
};

// Apply one editing command to an Input's text/caret/selection state.
// Single-line semantics: MoveLineStart/End span the whole value; multiline
// works the VISUAL (wrapped) line. The CARET is the MOVING end (see
// InputNode::selectionAnchor): an extended (Shift) move changes only the
// caret, leaving the anchor to span the selection. A move already at its
// bound is still consumed: the key targeted the focused input either way.
// `clipboard` is what Cut/Copy/Paste read/write; nullptr means none is
// installed (those commands report unconsumed). `m` measures the multiline
// line geometry (vertical moves, visual line bounds) — the same config-context
// measurer every other edit-path measurement reads.
EditOutcome applyEditCommand(InputNode& input, EditCommand cmd, bool extend, IClipboard* clipboard,
                             const ITextMeasurer* m) {
    std::string& s = input.displayText;
    // Land the caret: an unextended move collapses the anchor onto it, an
    // extended one leaves the anchor as the selection's fixed end.
    auto moveTo = [&](size_t pos) {
        input.caret = pos;
        if (!extend)
            input.selectionAnchor = pos;
    };
    // Erase the selected range; caret and anchor collapse onto its start.
    auto eraseSelection = [&] {
        size_t begin = input.selBegin();
        s.erase(begin, input.selEnd() - begin);
        input.caret = begin;
        input.selectionAnchor = begin;
    };
    switch (cmd) {
    // DOM collapse rule (the subtle part): an UNEXTENDED horizontal arrow
    // with an active selection collapses the selection to the corresponding
    // EDGE — MoveLeft to selBegin, MoveRight to selEnd — and moves NO further.
    // The arrow "lands" the selection rather than stepping past its edge;
    // only a collapsed caret (or an extended move) actually steps a code
    // point. Standard editor behavior.
    case EditCommand::MoveLeft:
        if (!extend && input.hasSelection())
            moveTo(input.selBegin());
        else
            moveTo(utf8::prevCodePoint(s, input.caret));
        return {true, false};
    case EditCommand::MoveRight:
        if (!extend && input.hasSelection())
            moveTo(input.selEnd());
        else
            moveTo(utf8::nextCodePoint(s, input.caret));
        return {true, false};
    case EditCommand::MoveLineStart: {
        // Single-line: the whole value IS the line. Multiline: the start of
        // the caret's VISUAL (wrapped) line — Home on a soft-wrapped
        // paragraph goes to the wrap point, not the paragraph start.
        if (!input.multiline()) {
            moveTo(0);
            return {true, false};
        }
        const auto& runs = input.displayRuns(input.wrapWidth(), m);
        moveTo(runs[input.caretPlacement(m).line].begin);
        return {true, false};
    }
    case EditCommand::MoveLineEnd: {
        if (!input.multiline()) {
            moveTo(s.size());
            return {true, false};
        }
        const auto& runs = input.displayRuns(input.wrapWidth(), m);
        moveTo(runs[input.caretPlacement(m).line].end);
        return {true, false};
    }
    case EditCommand::MoveUp:
    case EditCommand::MoveDown: {
        // Vertical caret navigation exists only in the wrapped line structure:
        // single-line stays unconsumed so a shim can route the key elsewhere.
        if (!input.multiline())
            return {};
        const auto& runs = input.displayRuns(input.wrapWidth(), m);
        InputNode::CaretPlacement place = input.caretPlacement(m);
        // Sticky goal column: the FIRST vertical move records the caret's x;
        // consecutive Up/Down steer toward it (a clamp at a short line does
        // not lose the column). Every other command resets it — see
        // handleEditCommand.
        if (!input.verticalNavGoalX)
            input.verticalNavGoalX = place.x;
        // At the boundary lines the move degenerates to start/end of the
        // text (the standard textarea behavior) — still consumed.
        if (cmd == EditCommand::MoveUp && place.line == 0) {
            moveTo(0);
            return {true, false};
        }
        if (cmd == EditCommand::MoveDown && place.line + 1 >= runs.size()) {
            moveTo(s.size());
            return {true, false};
        }
        size_t target = cmd == EditCommand::MoveUp ? place.line - 1 : place.line + 1;
        moveTo(input.indexAtLineX(target, *input.verticalNavGoalX, m));
        return {true, false};
    }
    case EditCommand::SelectAll:
        // Anchor at the front, caret (the moving end) at the back, so the
        // follow-scroll reveals the tail — matching editor convention.
        input.selectionAnchor = 0;
        input.caret = s.size();
        return {true, false};
    case EditCommand::DeleteBackward: {
        // With a selection, delete exactly the selection — NOT also the code
        // point before it (that's what a second keypress is for).
        if (input.hasSelection()) {
            eraseSelection();
            return {true, true};
        }
        if (input.caret == 0)
            return {true, false};
        // Erase the whole code point before the caret, never a lone byte, so
        // multi-byte characters delete cleanly and leave valid UTF-8 behind.
        size_t start = utf8::prevCodePoint(s, input.caret);
        s.erase(start, input.caret - start);
        input.caret = start;
        input.selectionAnchor = start;  // a delete always leaves a collapsed caret
        return {true, true};
    }
    case EditCommand::DeleteForward: {
        if (input.hasSelection()) {
            eraseSelection();
            return {true, true};
        }
        if (input.caret >= s.size())
            return {true, false};
        s.erase(input.caret, utf8::nextCodePoint(s, input.caret) - input.caret);
        return {true, true};
    }
    case EditCommand::Copy: {
        // Nothing selected: nothing to copy — do NOT clobber the clipboard's
        // current contents. Unconsumed, so a shim could route the key elsewhere.
        if (!input.hasSelection())
            return {};
        // Password copy is REFUSED (browser parity): the key is consumed so it
        // cannot leak elsewhere, but the secret never reaches the clipboard.
        if (input.props.password.value_or(false))
            return {true, false};
        // No clipboard installed: unconsumed, same fall-through contract.
        if (!clipboard)
            return {};
        clipboard->setText(s.substr(input.selBegin(), input.selEnd() - input.selBegin()));
        return {true, false};
    }
    case EditCommand::Cut: {
        // Password cut is refused whole (browser parity): neither the copy nor
        // the delete half runs — the secret stays put and off the clipboard.
        if (input.props.password.value_or(false))
            return {true, false};
        if (!input.hasSelection())
            return {};
        // No clipboard to receive the text ⇒ do NOT delete either: cutting into
        // the void would destroy text with nowhere to put it. Unconsumed.
        if (!clipboard)
            return {};
        // Copy-then-delete: the clipboard gets the selection, then the erase
        // collapses caret and anchor onto its start.
        clipboard->setText(s.substr(input.selBegin(), input.selEnd() - input.selBegin()));
        eraseSelection();
        return {true, true};
    }
    case EditCommand::Paste: {
        if (!clipboard)
            return {};
        std::string text = clipboard->getText();
        if (input.multiline()) {
            // Multiline paste KEEPS its newlines (the whole point of a
            // textarea), normalized to '\n': \r\n collapses, a lone \r (old-Mac
            // convention) maps — so caret math and wrapping see one newline
            // byte, matching what InsertNewline writes.
            std::string normalized;
            normalized.reserve(text.size());
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '\r') {
                    normalized.push_back('\n');
                    if (i + 1 < text.size() && text[i + 1] == '\n')
                        ++i;
                } else {
                    normalized.push_back(text[i]);
                }
            }
            text = std::move(normalized);
        } else {
            // Single-line sanitize: STRIP newlines (LF and CR) rather than
            // mapping them to spaces — the standard single-line behavior.
            std::erase_if(text, [](char c) { return c == '\n' || c == '\r'; });
        }
        // Pasting nothing is consumed but changes nothing — an empty paste is
        // not a delete, so an active selection survives it.
        if (text.empty())
            return {true, false};
        // Pasting over a selection REPLACES it (same rule as handleTextInput).
        if (input.hasSelection())
            eraseSelection();
        s.insert(input.caret, text);
        input.caret += text.size();
        input.selectionAnchor = input.caret;
        return {true, true};
    }
    case EditCommand::InsertNewline: {
        // Enter routes here from every shim; CORE decides by mode. Single-line
        // Enter is SUBMIT (the former Host::handleSubmit, folded into the
        // command path so shims carry exactly one Enter mapping); multiline
        // inserts '\n' at the caret, replacing any selection like typed text.
        if (!input.multiline())
            return {true, false, true};
        if (input.hasSelection())
            eraseSelection();
        s.insert(input.caret, 1, '\n');
        input.caret += 1;
        input.selectionAnchor = input.caret;
        return {true, true};
    }
    }
    return {};  // unreachable for a valid enum; -Wswitch flags new enumerators
}

}  // namespace

bool EventHandler::handleEditCommand(EditCommand cmd, bool extend, IClipboard* clipboard) noexcept {
    // Same liveness gate as handleTextInput: no focused (live) Input means no
    // edit — and NOT consumed, so the shim can route the key elsewhere.
    InputNode* focused = liveFocusedInput();
    if (!focused)
        return false;

    const ITextMeasurer* m = measurerOf(focused);
    EditOutcome outcome = applyEditCommand(*focused, cmd, extend, clipboard, m);
    if (!outcome.handled)
        return false;

    // Goal-column invalidation: any command OTHER than a vertical move resets
    // the sticky column, so the next Up/Down re-records from the caret's new
    // x. Up/Down themselves preserve it across a run of vertical moves.
    if (cmd != EditCommand::MoveUp && cmd != EditCommand::MoveDown)
        focused->verticalNavGoalX.reset();
    // Invalidate the wrap/measure state BEFORE the follow-scroll reads the
    // (multiline) line structure; latches the relayout for multiline.
    if (outcome.textChanged)
        noteTextEdited(focused);

    // Every consumed command restarts the blink (the caret shows while the user
    // works the keyboard) and latches the repaint: a caret move is a visual
    // transition exactly like a text edit (see handleTextInput). This fires even
    // for a no-op move/delete at a bound — the caret snapping visible IS a real
    // visual change — deliberately unlike C0's empty-backspace no-latch: the
    // philosophy is that any keyboard edit interaction resets the blink.
    focused->resetCaretBlink();
    // Follow-scroll AFTER the op: any caret move or deletion may push the
    // caret outside the visible span (or shrink the text under the scroll).
    focused->scrollCaretIntoView(m);
    markVisualStateChanged();

    if (outcome.textChanged && focused->props.onChange) {
        fireCallback(
            "onChange", [&] { focused->props.onChange(focused->displayText); },
            [this](std::string_view w, const std::exception* e) { reportError(w, e); });
    }
    // Single-line Enter (InsertNewline routed to a single-line input): fire
    // onSubmit — after the state bookkeeping above, like onChange.
    if (outcome.submit && focused->props.onSubmit) {
        fireCallback(
            "onSubmit", [&] { focused->props.onSubmit(); },
            [this](std::string_view w, const std::exception* e) { reportError(w, e); });
    }
    return true;
}

// Does `node` carry the handler matching the requested key phase?
// (KeyDown searches onKeyDown; KeyUp searches onKeyUp — so a node that registers
// only one of the two is reachable for the matching event and ignored for the other.)
bool EventHandler::hasKeyHandler(Node* node, KeyPhase phase) {
    const EventProps* p = eventPropsOf(node);
    if (!p)
        return false;
    return phase == KeyPhase::Down ? !!p->onKeyDown : !!p->onKeyUp;
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
