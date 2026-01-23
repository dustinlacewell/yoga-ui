#include <yui/core/Node.hpp>

#include <algorithm>
#include <cmath>

namespace yui {

// --- Node base ---

Node::Node() {
    yogaNode = YGNodeNew();
}

Node::~Node() {
    if (yogaNode) {
        YGNodeFree(yogaNode);
    }
}

void Node::applyLayoutProps(const LayoutProps& p) {
    // Dimensions
    if (p.width)
        YGNodeStyleSetWidth(yogaNode, *p.width);
    if (p.height)
        YGNodeStyleSetHeight(yogaNode, *p.height);
    if (p.minWidth)
        YGNodeStyleSetMinWidth(yogaNode, *p.minWidth);
    if (p.minHeight)
        YGNodeStyleSetMinHeight(yogaNode, *p.minHeight);
    if (p.maxWidth)
        YGNodeStyleSetMaxWidth(yogaNode, *p.maxWidth);
    if (p.maxHeight)
        YGNodeStyleSetMaxHeight(yogaNode, *p.maxHeight);

    // Flex
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

    // Padding
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

    // Margin
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

    // Gap
    if (p.gap)
        YGNodeStyleSetGap(yogaNode, YGGutterAll, *p.gap);
    if (p.rowGap)
        YGNodeStyleSetGap(yogaNode, YGGutterRow, *p.rowGap);
    if (p.columnGap)
        YGNodeStyleSetGap(yogaNode, YGGutterColumn, *p.columnGap);

    // Absolute positioning
    if (p.positionLeft)
        YGNodeStyleSetPosition(yogaNode, YGEdgeLeft, *p.positionLeft);
    if (p.positionTop)
        YGNodeStyleSetPosition(yogaNode, YGEdgeTop, *p.positionTop);
    if (p.positionRight)
        YGNodeStyleSetPosition(yogaNode, YGEdgeRight, *p.positionRight);
    if (p.positionBottom)
        YGNodeStyleSetPosition(yogaNode, YGEdgeBottom, *p.positionBottom);

    // Aspect ratio
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

// Forward declaration
static void layoutScrollContent(Node* node);

void Node::calculateLayout(float availableWidth, float availableHeight) {
    YGNodeCalculateLayout(yogaNode, availableWidth, availableHeight, YGDirectionLTR);
    syncLayoutFromYoga();
    // After main layout, calculate ScrollNode children with unconstrained height
    layoutScrollContent(this);
}

bool Node::update(float dt) {
    bool animating = false;

    // Update this node if it's a ScrollNode
    if (type() == PrimitiveType::Scroll) {
        auto* scrollNode = static_cast<ScrollNode*>(this);
        if (scrollNode->updateSmooth(dt)) {
            animating = true;
        }
    }

    // Recurse into children
    for (auto& child : children) {
        if (child->update(dt)) {
            animating = true;
        }
    }

    return animating;
}

// Recursively find ScrollNodes and layout their children as separate Yoga roots
static void layoutScrollContent(Node* node) {
    if (node->type() == PrimitiveType::Scroll) {
        auto* scrollNode = static_cast<ScrollNode*>(node);
        // Layout children with scroll container's width but unconstrained height
        for (auto& child : scrollNode->children) {
            YGNodeCalculateLayout(child->yogaNode, scrollNode->layout.width, YGUndefined, YGDirectionLTR);
            child->syncLayoutFromYoga();
        }
        scrollNode->updateContentSize();
    }
    // Recurse into children (handles nested ScrollNodes)
    for (auto& child : node->children) {
        layoutScrollContent(child.get());
    }
}

// --- BoxNode ---

BoxNode::BoxNode() = default;

void BoxNode::updateProps(const PropsVariant& p) {
    props = std::get<BoxProps>(p);
    applyLayoutProps(props);
}

// --- TextNode ---

TextNode::TextNode() {
    setupMeasureFunc();
}

void TextNode::updateProps(const PropsVariant& p) {
    props = std::get<TextProps>(p);
    applyLayoutProps(props);
    // Mark layout dirty since text content may have changed
    YGNodeMarkDirty(yogaNode);
}

void TextNode::setupMeasureFunc() {
    YGNodeSetContext(yogaNode, this);
    YGNodeSetMeasureFunc(yogaNode, &TextNode::measureFunc);
}

YGSize TextNode::measureFunc(YGNodeConstRef node, float width, YGMeasureMode widthMode, float /*height*/,
                             YGMeasureMode /*heightMode*/) {
    auto* textNode = static_cast<TextNode*>(YGNodeGetContext(node));
    if (!textNode) {
        return {0, 0};
    }

    float fontSize = textNode->props.fontSize.value_or(12.0f);
    float maxWidth = (widthMode == YGMeasureModeUndefined) ? 0 : width;

    Size size = Measure::measureText(textNode->props.text, fontSize, maxWidth);

    return {size.width, size.height};
}

// --- InputNode ---

InputNode::InputNode() = default;

void InputNode::updateProps(const PropsVariant& p) {
    props = std::get<InputProps>(p);
    applyLayoutProps(props);
}

// --- ScrollNode ---

ScrollNode::ScrollNode() {
    // Children are laid out separately with unconstrained height (see layoutScrollContent)
    // so YGOverflowScroll isn't strictly needed, but set it for semantic clarity
    YGNodeStyleSetOverflow(yogaNode, YGOverflowScroll);
}

void ScrollNode::updateProps(const PropsVariant& p) {
    props = std::get<ScrollProps>(p);
    applyLayoutProps(props);
}

void ScrollNode::updateContentSize() {
    // With YGAlignFlexStart, children are sized to their intrinsic dimensions.
    // Content size is simply the bounding box of direct children.
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
    // Exponential smoothing - reaches ~95% of target in ~0.15 seconds
    constexpr float smoothingSpeed = 20.0f;
    float t = 1.0f - std::exp(-smoothingSpeed * dt);

    float dx = targetScrollX - scrollOffsetX;
    float dy = targetScrollY - scrollOffsetY;

    scrollOffsetX += dx * t;
    scrollOffsetY += dy * t;

    // Snap to target if very close
    constexpr float snapThreshold = 0.5f;
    if (std::abs(dx) < snapThreshold && std::abs(dy) < snapThreshold) {
        scrollOffsetX = targetScrollX;
        scrollOffsetY = targetScrollY;
        return false;  // Done animating
    }
    return true;  // Still animating
}

// --- CanvasNode ---

CanvasNode::CanvasNode() = default;

void CanvasNode::updateProps(const PropsVariant& p) {
    props = std::get<CanvasProps>(p);
    applyLayoutProps(props);
}

// --- Factory ---

std::unique_ptr<Node> createNode(PrimitiveType type) {
    switch (type) {
    case PrimitiveType::Box:
        return std::make_unique<BoxNode>();
    case PrimitiveType::Text:
        return std::make_unique<TextNode>();
    case PrimitiveType::Input:
        return std::make_unique<InputNode>();
    case PrimitiveType::Scroll:
        return std::make_unique<ScrollNode>();
    case PrimitiveType::Canvas:
        return std::make_unique<CanvasNode>();
    }
    return nullptr;
}

}  // namespace yui
