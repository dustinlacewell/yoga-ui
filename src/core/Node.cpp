#include <yui/core/Node.hpp>

#include <algorithm>
#include <cmath>

namespace yui {

// --- Node base ---

Node::Node(YGConfigRef config) {
    // Yoga requires a non-null config; mirror YGNodeNew() by falling back to the
    // default config when none was supplied (e.g. a host-less test reconciler).
    yogaNode = YGNodeNewWithConfig(config ? config : YGConfigGetDefault());
}

Node::~Node() {
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

    for (auto& child : children) {
        child->syncLayoutFromYoga();
    }
}

static void layoutScrollContent(Node* node);

void Node::calculateLayout(float availableWidth, float availableHeight) {
    YGNodeCalculateLayout(yogaNode, availableWidth, availableHeight, YGDirectionLTR);
    syncLayoutFromYoga();
    layoutScrollContent(this);
}

bool Node::update(float dt) {
    bool animating = false;

    if (type() == PrimitiveType::Scroll) {
        auto* scrollNode = static_cast<ScrollNode*>(this);
        if (scrollNode->updateSmooth(dt)) {
            animating = true;
        }
    }

    for (auto& child : children) {
        if (child->update(dt)) {
            animating = true;
        }
    }

    return animating;
}

static void layoutScrollContent(Node* node) {
    if (node->type() == PrimitiveType::Scroll) {
        auto* scrollNode = static_cast<ScrollNode*>(node);
        for (auto& child : scrollNode->children) {
            if (child->yogaNode) {
                YGNodeCalculateLayout(child->yogaNode, scrollNode->layout.width, YGUndefined, YGDirectionLTR);
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

void BoxNode::updateProps(const PropsVariant& p) {
    const auto& newProps = std::get<BoxProps>(p);
    bool layoutChanged = static_cast<const LayoutProps&>(newProps) != static_cast<const LayoutProps&>(props);
    props = newProps;
    if (layoutChanged) {
        applyLayoutProps(props);
    }
}

// --- TextNode ---

TextNode::TextNode(YGConfigRef config) : Node(config) {
    setupMeasureFunc();
}

void TextNode::updateProps(const PropsVariant& p) {
    const auto& newProps = std::get<TextProps>(p);
    bool layoutChanged = static_cast<const LayoutProps&>(newProps) != static_cast<const LayoutProps&>(props);
    bool textChanged = newProps.text != props.text || newProps.fontSize != props.fontSize;
    props = newProps;
    if (layoutChanged) {
        applyLayoutProps(props);
    }
    if (textChanged) {
        YGNodeMarkDirty(yogaNode);
    }
}

void TextNode::setupMeasureFunc() {
    YGNodeSetContext(yogaNode, this);
    YGNodeSetMeasureFunc(yogaNode, &TextNode::measureFunc);
}

YGSize TextNode::measureFunc(YGNodeConstRef node, float width, YGMeasureMode widthMode, float height,
                             YGMeasureMode heightMode) {
    auto* textNode = static_cast<TextNode*>(YGNodeGetContext(node));
    if (!textNode) {
        return {0, 0};
    }

    float fontSize = textNode->props.fontSize.value_or(12.0f);
    float maxWidth = (widthMode == YGMeasureModeUndefined) ? 0 : width;

    // The host's text measurer lives in the per-host Yoga config context. Recover
    // it from this node's config; fall back to the heuristic when none is set.
    auto* measurer = static_cast<const ITextMeasurer*>(
        YGConfigGetContext(YGNodeGetConfig(const_cast<YGNodeRef>(node))));
    Size size = measurer ? measurer->measure(textNode->props.text, fontSize, maxWidth)
                         : fallbackMeasure(textNode->props.text, fontSize, maxWidth);
    return {size.width, size.height};
}

// --- InputNode ---

InputNode::InputNode(YGConfigRef config) : Node(config) {}

void InputNode::updateProps(const PropsVariant& p) {
    const auto& newProps = std::get<InputProps>(p);
    bool layoutChanged = static_cast<const LayoutProps&>(newProps) != static_cast<const LayoutProps&>(props);
    props = newProps;
    if (layoutChanged) {
        applyLayoutProps(props);
    }
    displayText = props.value;
}

// --- ScrollNode ---

ScrollNode::ScrollNode(YGConfigRef config) : Node(config) {
    YGNodeStyleSetOverflow(yogaNode, YGOverflowScroll);
}

void ScrollNode::updateProps(const PropsVariant& p) {
    const auto& newProps = std::get<ScrollProps>(p);
    bool layoutChanged = static_cast<const LayoutProps&>(newProps) != static_cast<const LayoutProps&>(props);
    props = newProps;
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
    float maxScrollX = std::max(0.0f, contentWidth - layout.width);
    float maxScrollY = std::max(0.0f, contentHeight - layout.height);

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

void CanvasNode::updateProps(const PropsVariant& p) {
    const auto& newProps = std::get<CanvasProps>(p);
    bool layoutChanged = static_cast<const LayoutProps&>(newProps) != static_cast<const LayoutProps&>(props);
    props = newProps;
    if (layoutChanged) {
        applyLayoutProps(props);
    }
}

// --- Factory ---

std::unique_ptr<Node> createNode(PrimitiveType type, YGConfigRef config) {
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
    return nullptr;
}

}  // namespace yui
