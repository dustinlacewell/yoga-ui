#include <yui/core/Node.hpp>

#include <yui/core/RenderDefaults.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace yui {

// --- Node base ---

Node::Node(YGConfigRef config) {
    // Yoga requires a non-null config; mirror YGNodeNew() by falling back to the
    // default config when none was supplied (e.g. a host-less test reconciler).
    yogaNode = YGNodeNewWithConfig(config ? config : YGConfigGetDefault());
}

Node::~Node() {
    *alive = false;  // signal holders (e.g. EventHandler::focusedInput_) we are gone
    if (yogaNode) {
        YGNodeFree(yogaNode);
    }
}

void Node::applyLayoutProps(const LayoutProps& p) {
    // Reset all dimension/layout styles to defaults first.
    YGNodeStyleSetWidth(yogaNode, YGUndefined);
    YGNodeStyleSetHeight(yogaNode, YGUndefined);
    YGNodeStyleSetMinWidth(yogaNode, YGUndefined);
    YGNodeStyleSetMinHeight(yogaNode, YGUndefined);
    YGNodeStyleSetMaxWidth(yogaNode, YGUndefined);
    YGNodeStyleSetMaxHeight(yogaNode, YGUndefined);
    YGNodeStyleSetFlexGrow(yogaNode, 0);
    YGNodeStyleSetFlexShrink(yogaNode, 0);
    YGNodeStyleSetFlexBasis(yogaNode, YGUndefined);
    YGNodeStyleSetFlexDirection(yogaNode, YGFlexDirectionColumn);
    YGNodeStyleSetFlexWrap(yogaNode, YGWrapNoWrap);
    YGNodeStyleSetJustifyContent(yogaNode, YGJustifyFlexStart);
    YGNodeStyleSetAlignItems(yogaNode, YGAlignStretch);
    YGNodeStyleSetAlignContent(yogaNode, YGAlignFlexStart);
    YGNodeStyleSetAlignSelf(yogaNode, YGAlignAuto);
    YGNodeStyleSetPositionType(yogaNode, YGPositionTypeRelative);
    YGNodeStyleSetDisplay(yogaNode, YGDisplayFlex);
    YGNodeStyleSetPadding(yogaNode, YGEdgeAll, YGUndefined);
    YGNodeStyleSetPadding(yogaNode, YGEdgeTop, YGUndefined);
    YGNodeStyleSetPadding(yogaNode, YGEdgeRight, YGUndefined);
    YGNodeStyleSetPadding(yogaNode, YGEdgeBottom, YGUndefined);
    YGNodeStyleSetPadding(yogaNode, YGEdgeLeft, YGUndefined);
    YGNodeStyleSetMargin(yogaNode, YGEdgeAll, YGUndefined);
    YGNodeStyleSetMargin(yogaNode, YGEdgeTop, YGUndefined);
    YGNodeStyleSetMargin(yogaNode, YGEdgeRight, YGUndefined);
    YGNodeStyleSetMargin(yogaNode, YGEdgeBottom, YGUndefined);
    YGNodeStyleSetMargin(yogaNode, YGEdgeLeft, YGUndefined);
    YGNodeStyleSetGap(yogaNode, YGGutterAll, YGUndefined);
    YGNodeStyleSetGap(yogaNode, YGGutterRow, YGUndefined);
    YGNodeStyleSetGap(yogaNode, YGGutterColumn, YGUndefined);
    YGNodeStyleSetPosition(yogaNode, YGEdgeLeft, YGUndefined);
    YGNodeStyleSetPosition(yogaNode, YGEdgeTop, YGUndefined);
    YGNodeStyleSetPosition(yogaNode, YGEdgeRight, YGUndefined);
    YGNodeStyleSetPosition(yogaNode, YGEdgeBottom, YGUndefined);
    YGNodeStyleSetAspectRatio(yogaNode, YGUndefined);

    // Now apply the new props
    if (p.width)
        YGNodeStyleSetWidth(yogaNode, *p.width);
    if (p.widthPercent)
        YGNodeStyleSetWidthPercent(yogaNode, *p.widthPercent);
    if (p.height)
        YGNodeStyleSetHeight(yogaNode, *p.height);
    if (p.heightPercent)
        YGNodeStyleSetHeightPercent(yogaNode, *p.heightPercent);
    if (p.minWidth)
        YGNodeStyleSetMinWidth(yogaNode, *p.minWidth);
    if (p.minHeight)
        YGNodeStyleSetMinHeight(yogaNode, *p.minHeight);
    if (p.maxWidth)
        YGNodeStyleSetMaxWidth(yogaNode, *p.maxWidth);
    if (p.maxHeight)
        YGNodeStyleSetMaxHeight(yogaNode, *p.maxHeight);

    if (p.flexGrow)
        YGNodeStyleSetFlexGrow(yogaNode, *p.flexGrow);
    if (p.flexShrink)
        YGNodeStyleSetFlexShrink(yogaNode, *p.flexShrink);
    if (p.flexBasis)
        YGNodeStyleSetFlexBasis(yogaNode, *p.flexBasis);

    if (p.flexDirection) {
        YGFlexDirection dir;
        switch (*p.flexDirection) {
        case FlexDirection::Row:
            dir = YGFlexDirectionRow;
            break;
        case FlexDirection::Column:
            dir = YGFlexDirectionColumn;
            break;
        case FlexDirection::RowReverse:
            dir = YGFlexDirectionRowReverse;
            break;
        case FlexDirection::ColumnReverse:
            dir = YGFlexDirectionColumnReverse;
            break;
        }
        YGNodeStyleSetFlexDirection(yogaNode, dir);
    }

    if (p.flexWrap) {
        YGWrap wrap;
        switch (*p.flexWrap) {
        case FlexWrap::NoWrap:
            wrap = YGWrapNoWrap;
            break;
        case FlexWrap::Wrap:
            wrap = YGWrapWrap;
            break;
        case FlexWrap::WrapReverse:
            wrap = YGWrapWrapReverse;
            break;
        }
        YGNodeStyleSetFlexWrap(yogaNode, wrap);
    }

    if (p.justifyContent) {
        YGJustify justify;
        switch (*p.justifyContent) {
        case JustifyContent::FlexStart:
            justify = YGJustifyFlexStart;
            break;
        case JustifyContent::Center:
            justify = YGJustifyCenter;
            break;
        case JustifyContent::FlexEnd:
            justify = YGJustifyFlexEnd;
            break;
        case JustifyContent::SpaceBetween:
            justify = YGJustifySpaceBetween;
            break;
        case JustifyContent::SpaceAround:
            justify = YGJustifySpaceAround;
            break;
        case JustifyContent::SpaceEvenly:
            justify = YGJustifySpaceEvenly;
            break;
        }
        YGNodeStyleSetJustifyContent(yogaNode, justify);
    }

    if (p.alignItems) {
        YGAlign align;
        switch (*p.alignItems) {
        case AlignItems::FlexStart:
            align = YGAlignFlexStart;
            break;
        case AlignItems::Center:
            align = YGAlignCenter;
            break;
        case AlignItems::FlexEnd:
            align = YGAlignFlexEnd;
            break;
        case AlignItems::Stretch:
            align = YGAlignStretch;
            break;
        case AlignItems::Baseline:
            align = YGAlignBaseline;
            break;
        }
        YGNodeStyleSetAlignItems(yogaNode, align);
    }

    if (p.alignContent) {
        YGAlign align;
        switch (*p.alignContent) {
        case AlignContent::FlexStart:
            align = YGAlignFlexStart;
            break;
        case AlignContent::Center:
            align = YGAlignCenter;
            break;
        case AlignContent::FlexEnd:
            align = YGAlignFlexEnd;
            break;
        case AlignContent::Stretch:
            align = YGAlignStretch;
            break;
        case AlignContent::SpaceBetween:
            align = YGAlignSpaceBetween;
            break;
        case AlignContent::SpaceAround:
            align = YGAlignSpaceAround;
            break;
        case AlignContent::SpaceEvenly:
            align = YGAlignSpaceEvenly;
            break;
        }
        YGNodeStyleSetAlignContent(yogaNode, align);
    }

    if (p.alignSelf) {
        YGAlign align;
        switch (*p.alignSelf) {
        case AlignSelf::Auto:
            align = YGAlignAuto;
            break;
        case AlignSelf::FlexStart:
            align = YGAlignFlexStart;
            break;
        case AlignSelf::Center:
            align = YGAlignCenter;
            break;
        case AlignSelf::FlexEnd:
            align = YGAlignFlexEnd;
            break;
        case AlignSelf::Stretch:
            align = YGAlignStretch;
            break;
        case AlignSelf::Baseline:
            align = YGAlignBaseline;
            break;
        }
        YGNodeStyleSetAlignSelf(yogaNode, align);
    }

    if (p.positionType) {
        YGPositionType pos;
        switch (*p.positionType) {
        case PositionType::Relative:
            pos = YGPositionTypeRelative;
            break;
        case PositionType::Absolute:
            pos = YGPositionTypeAbsolute;
            break;
        }
        YGNodeStyleSetPositionType(yogaNode, pos);
    }

    if (p.display) {
        YGDisplay disp;
        switch (*p.display) {
        case Display::Flex:
            disp = YGDisplayFlex;
            break;
        case Display::None:
            disp = YGDisplayNone;
            break;
        }
        YGNodeStyleSetDisplay(yogaNode, disp);
    }

    if (p.padding)
        YGNodeStyleSetPadding(yogaNode, YGEdgeAll, *p.padding);
    if (p.paddingTop)
        YGNodeStyleSetPadding(yogaNode, YGEdgeTop, *p.paddingTop);
    if (p.paddingRight)
        YGNodeStyleSetPadding(yogaNode, YGEdgeRight, *p.paddingRight);
    if (p.paddingBottom)
        YGNodeStyleSetPadding(yogaNode, YGEdgeBottom, *p.paddingBottom);
    if (p.paddingLeft)
        YGNodeStyleSetPadding(yogaNode, YGEdgeLeft, *p.paddingLeft);

    if (p.margin)
        YGNodeStyleSetMargin(yogaNode, YGEdgeAll, *p.margin);
    if (p.marginTop)
        YGNodeStyleSetMargin(yogaNode, YGEdgeTop, *p.marginTop);
    if (p.marginRight)
        YGNodeStyleSetMargin(yogaNode, YGEdgeRight, *p.marginRight);
    if (p.marginBottom)
        YGNodeStyleSetMargin(yogaNode, YGEdgeBottom, *p.marginBottom);
    if (p.marginLeft)
        YGNodeStyleSetMargin(yogaNode, YGEdgeLeft, *p.marginLeft);

    if (p.gap)
        YGNodeStyleSetGap(yogaNode, YGGutterAll, *p.gap);
    if (p.rowGap)
        YGNodeStyleSetGap(yogaNode, YGGutterRow, *p.rowGap);
    if (p.columnGap)
        YGNodeStyleSetGap(yogaNode, YGGutterColumn, *p.columnGap);

    if (p.positionLeft)
        YGNodeStyleSetPosition(yogaNode, YGEdgeLeft, *p.positionLeft);
    if (p.positionTop)
        YGNodeStyleSetPosition(yogaNode, YGEdgeTop, *p.positionTop);
    if (p.positionRight)
        YGNodeStyleSetPosition(yogaNode, YGEdgeRight, *p.positionRight);
    if (p.positionBottom)
        YGNodeStyleSetPosition(yogaNode, YGEdgeBottom, *p.positionBottom);

    if (p.aspectRatio)
        YGNodeStyleSetAspectRatio(yogaNode, *p.aspectRatio);
}

void Node::syncLayoutFromYoga() {
    layout.left = YGNodeLayoutGetLeft(yogaNode);
    layout.top = YGNodeLayoutGetTop(yogaNode);
    layout.width = YGNodeLayoutGetWidth(yogaNode);
    layout.height = YGNodeLayoutGetHeight(yogaNode);
    layout.insetLeft = YGNodeLayoutGetPadding(yogaNode, YGEdgeLeft) + YGNodeLayoutGetBorder(yogaNode, YGEdgeLeft);
    layout.insetTop = YGNodeLayoutGetPadding(yogaNode, YGEdgeTop) + YGNodeLayoutGetBorder(yogaNode, YGEdgeTop);
    layout.insetRight = YGNodeLayoutGetPadding(yogaNode, YGEdgeRight) + YGNodeLayoutGetBorder(yogaNode, YGEdgeRight);
    layout.insetBottom =
        YGNodeLayoutGetPadding(yogaNode, YGEdgeBottom) + YGNodeLayoutGetBorder(yogaNode, YGEdgeBottom);

    for (auto& child : children) {
        child->syncLayoutFromYoga();
    }

    // Subtree AABB, folded bottom-up (children just synced theirs above): own
    // border box unioned with each child's subtree bounds, shifted from the
    // child's parent-relative space into this node's. Hit testing prunes by
    // this box so unclipped overflowing children stay clickable.
    layout.subtreeLeft = layout.left;
    layout.subtreeTop = layout.top;
    layout.subtreeRight = layout.left + layout.width;
    layout.subtreeBottom = layout.top + layout.height;

    // A Scroll CLIPS its content: overflow is invisible, so it must not widen
    // the hit-test prune of the scroll or any ancestor. (Scroll children also
    // lay out in a detached, scroll-offset coordinate space — their bounds are
    // not commensurable with this node's parent space anyway.)
    if (type() == PrimitiveType::Scroll)
        return;

    for (auto& child : children) {
        layout.subtreeLeft = std::min(layout.subtreeLeft, layout.left + child->layout.subtreeLeft);
        layout.subtreeTop = std::min(layout.subtreeTop, layout.top + child->layout.subtreeTop);
        layout.subtreeRight = std::max(layout.subtreeRight, layout.left + child->layout.subtreeRight);
        layout.subtreeBottom = std::max(layout.subtreeBottom, layout.top + child->layout.subtreeBottom);
    }
}

static void layoutScrollContent(Node* node);

void Node::calculateLayout(float availableWidth, float availableHeight) {
    YGNodeCalculateLayout(yogaNode, availableWidth, availableHeight, YGDirectionLTR);
    syncLayoutFromYoga();
    layoutScrollContent(this);
}

AnimationResult Node::update(float dt) {
    AnimationResult result;

    if (type() == PrimitiveType::Scroll) {
        auto* scrollNode = static_cast<ScrollNode*>(this);
        if (scrollNode->updateSmooth(dt)) {
            // The offsets moved this frame, so an animated scroll repaints
            // every frame it is still animating.
            result.animating = true;
            result.needsRepaint = true;
        }
    }

    if (type() == PrimitiveType::Input) {
        auto* input = static_cast<InputNode*>(this);
        if (input->focused) {
            // The blink phase advances every focused frame (animating), but
            // pixels change only when visibility toggles (the repaint edge).
            result.animating = true;
            result.needsRepaint |= input->updateBlink(dt);
        }
    }

    for (auto& child : children) {
        AnimationResult childResult = child->update(dt);
        result.animating |= childResult.animating;
        result.needsRepaint |= childResult.needsRepaint;
    }

    return result;
}

static void layoutScrollContent(Node* node) {
    if (node->type() == PrimitiveType::Scroll) {
        auto* scrollNode = static_cast<ScrollNode*>(node);
        for (auto& child : scrollNode->children) {
            if (child->yogaNode) {
                // The detached content root lays out against the scroll's CONTENT
                // width: the scroll's own insets (synced by the enclosing
                // calculateLayout before this runs) shrink the viewport, so
                // Scroll padding is honored the same way Box padding is.
                YGNodeCalculateLayout(child->yogaNode, scrollNode->layout.contentWidth(), YGUndefined, YGDirectionLTR);
                child->syncLayoutFromYoga();
            }
        }
        scrollNode->updateContentSize();
        scrollNode->clampScrollOffset();
    }
    for (auto& child : node->children) {
        layoutScrollContent(child.get());
    }
}

// --- BoxNode ---

BoxNode::BoxNode(YGConfigRef config) : Node(config) {}

void BoxNode::updateProps(PropsVariant&& p) {
    // Compute the change predicate against the CURRENT member props BEFORE the
    // move overwrites them, then move the new props in.
    auto& newProps = std::get<BoxProps>(p);
    bool layoutChanged = static_cast<const LayoutProps&>(newProps) != static_cast<const LayoutProps&>(props);
    props = std::move(newProps);
    if (layoutChanged) {
        applyLayoutProps(props);
    }
}

// --- TextNode ---

TextNode::TextNode(YGConfigRef config) : Node(config) {
    setupMeasureFunc();
}

void TextNode::updateProps(PropsVariant&& p) {
    // Compute both predicates against the CURRENT member props BEFORE the move.
    auto& newProps = std::get<TextProps>(p);
    bool layoutChanged = static_cast<const LayoutProps&>(newProps) != static_cast<const LayoutProps&>(props);
    // The font affects measured width as much as the text/size do; without it in
    // the predicate a `.font()`-only change would not re-mark the Yoga node dirty
    // and would keep a stale measurement (the mis-sized-glyph class of bug).
    // `wrap` toggles soft wrapping, which changes both the wrap result and the
    // measured size, so it invalidates the same way.
    bool textChanged = newProps.text != props.text || newProps.fontSize != props.fontSize
                       || newProps.font != props.font || newProps.wrap != props.wrap;
    props = std::move(newProps);
    if (layoutChanged) {
        applyLayoutProps(props);
    }
    if (textChanged) {
        wrapCache_.valid = false;
        YGNodeMarkDirty(yogaNode);
    }
}

void TextNode::setupMeasureFunc() {
    YGNodeSetContext(yogaNode, this);
    YGNodeSetMeasureFunc(yogaNode, &TextNode::measureFunc);
}

YGSize TextNode::measureFunc(YGNodeConstRef node, float width, YGMeasureMode widthMode,
                             [[maybe_unused]] float height,
                             [[maybe_unused]] YGMeasureMode heightMode) {
    auto* textNode = static_cast<TextNode*>(YGNodeGetContext(node));
    if (!textNode) {
        return {0, 0};
    }

    float fontSize = textNode->props.fontSize.value_or(render_defaults::kDefaultFontSize);
    // wrap(false) opts out of soft wrapping: measure unconstrained so the text
    // stays a single run regardless of the available width.
    bool wrapOn = textNode->props.wrap.value_or(true);
    float maxWidth = (!wrapOn || widthMode == YGMeasureModeUndefined) ? 0 : width;
    // Measure in the node's requested font face (empty ⇒ default) so the measured
    // size matches what drawText will render with.
    std::string_view font = textNode->props.font ? std::string_view(*textNode->props.font) : std::string_view{};

    // The host's text measurer lives in the per-host Yoga config context. Recover
    // it from this node's config; wrappedRuns falls back to the heuristic when
    // none is set. Reading through the node's wrap cache keeps this and the tree
    // renderer on the SAME wrap.
    auto* measurer = static_cast<const ITextMeasurer*>(
        YGConfigGetContext(YGNodeGetConfig(const_cast<YGNodeRef>(node))));
    const auto& runs = textNode->wrappedRuns(maxWidth, measurer);
    FontMetrics metrics = measurer ? measurer->fontMetrics(fontSize, font) : fallbackFontMetrics(fontSize, font);
    Size size = render::runsSize(runs, metrics.lineHeight);
    return {size.width, size.height};
}

const std::vector<render::TextRun>& TextNode::wrappedRuns(float maxWidth, const ITextMeasurer* measurer) const {
    if (wrapCache_.valid && wrapCache_.measurer == measurer && wrapCache_.maxWidth == maxWidth) {
        return wrapCache_.runs;
    }

    float fontSize = props.fontSize.value_or(render_defaults::kDefaultFontSize);
    std::string_view font = props.font ? std::string_view(*props.font) : std::string_view{};
    wrapCache_.runs = render::wrapText(props.text, maxWidth, [&](std::string_view run) {
        return measurer ? measurer->measureRun(run, fontSize, font) : fallbackMeasureRun(run, fontSize, font);
    });
    wrapCache_.measurer = measurer;
    wrapCache_.maxWidth = maxWidth;
    wrapCache_.valid = true;
    return wrapCache_.runs;
}

// --- InputNode ---

InputNode::InputNode(YGConfigRef config) : Node(config) {}

void InputNode::updateProps(PropsVariant&& p) {
    auto& newProps = std::get<InputProps>(p);
    bool layoutChanged = static_cast<const LayoutProps&>(newProps) != static_cast<const LayoutProps&>(props);
    props = std::move(newProps);
    if (layoutChanged) {
        applyLayoutProps(props);
    }
    // Read displayText from the MOVED-IN member (props.value), not the moved-from
    // source (newProps.value is now empty).
    displayText = props.value;
}

bool InputNode::updateBlink(float dt) {
    namespace rd = render_defaults;
    blinkPhaseMs_ = std::fmod(blinkPhaseMs_ + dt * 1000.0f, static_cast<float>(rd::kCaretBlinkPeriodMs));
    bool nowVisible = blinkPhaseMs_ < static_cast<float>(rd::kCaretBlinkOnMs);
    bool toggled = nowVisible != caretVisible;
    caretVisible = nowVisible;
    return toggled;
}

// --- ScrollNode ---

ScrollNode::ScrollNode(YGConfigRef config) : Node(config) {
    YGNodeStyleSetOverflow(yogaNode, YGOverflowScroll);
}

void ScrollNode::updateProps(PropsVariant&& p) {
    auto& newProps = std::get<ScrollProps>(p);
    bool layoutChanged = static_cast<const LayoutProps&>(newProps) != static_cast<const LayoutProps&>(props);
    props = std::move(newProps);
    if (layoutChanged) {
        applyLayoutProps(props);
    }
}

void ScrollNode::updateContentSize() {
    contentWidth = 0;
    contentHeight = 0;

    for (auto& child : children) {
        float childRight = child->layout.left + child->layout.width;
        float childBottom = child->layout.top + child->layout.height;
        if (childRight > contentWidth)
            contentWidth = childRight;
        if (childBottom > contentHeight)
            contentHeight = childBottom;
    }
}

void ScrollNode::clampScrollOffset() {
    // The viewport is the border box minus the scroll's own insets — content is
    // clipped to it, so scrolling ends when the content's far edge meets the
    // padded viewport's far edge, not the border box's.
    float maxScrollX = std::max(0.0f, contentWidth - layout.contentWidth());
    float maxScrollY = std::max(0.0f, contentHeight - layout.contentHeight());

    targetScrollX = std::max(0.0f, std::min(targetScrollX, maxScrollX));
    targetScrollY = std::max(0.0f, std::min(targetScrollY, maxScrollY));
    scrollOffsetX = std::max(0.0f, std::min(scrollOffsetX, maxScrollX));
    scrollOffsetY = std::max(0.0f, std::min(scrollOffsetY, maxScrollY));
}

bool ScrollNode::updateSmooth(float dt) {
    constexpr float smoothingSpeed = 20.0f;
    float t = 1.0f - std::exp(-smoothingSpeed * dt);

    float dx = targetScrollX - scrollOffsetX;
    float dy = targetScrollY - scrollOffsetY;

    scrollOffsetX += dx * t;
    scrollOffsetY += dy * t;

    constexpr float snapThreshold = 0.5f;
    if (std::abs(dx) < snapThreshold && std::abs(dy) < snapThreshold) {
        scrollOffsetX = targetScrollX;
        scrollOffsetY = targetScrollY;
        return false;
    }
    return true;
}

// --- CanvasNode ---

CanvasNode::CanvasNode(YGConfigRef config) : Node(config) {}

void CanvasNode::updateProps(PropsVariant&& p) {
    auto& newProps = std::get<CanvasProps>(p);
    bool layoutChanged = static_cast<const LayoutProps&>(newProps) != static_cast<const LayoutProps&>(props);
    props = std::move(newProps);
    if (layoutChanged) {
        applyLayoutProps(props);
    }
}

// --- Factory ---

std::unique_ptr<Node> createNode(PrimitiveType type, YGConfigRef config) {
    // Every enumerator is cased and returns directly, with NO default: that keeps
    // -Wswitch exhaustiveness live, so adding a PrimitiveType without a node here
    // is a compile-time warning. Falling through the switch means `type` held a
    // value outside the enum (corrupt/out-of-range) — the callers (Reconciler)
    // immediately dereference the result, so failing loudly here localizes the
    // fault instead of surfacing as a downstream null-deref. There is no error
    // sink reachable from this free function, so assert; in release we still
    // return nullptr rather than fabricate a node of the wrong type.
    switch (type) {
    case PrimitiveType::Box:
        return std::make_unique<BoxNode>(config);
    case PrimitiveType::Text:
        return std::make_unique<TextNode>(config);
    case PrimitiveType::Input:
        return std::make_unique<InputNode>(config);
    case PrimitiveType::Scroll:
        return std::make_unique<ScrollNode>(config);
    case PrimitiveType::Canvas:
        return std::make_unique<CanvasNode>(config);
    }
    assert(false && "createNode: PrimitiveType out of range");
    return nullptr;
}

}  // namespace yui
