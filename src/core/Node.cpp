#include <yui/core/Node.hpp>

#include <yui/core/NodeRef.hpp>  // absoluteRect (scrollIntoView)
#include <yui/core/RenderDefaults.hpp>
#include <yui/core/Utf8.hpp>
#include <yui/render/StyleResolver.hpp>  // resolveInput (Input text geometry font)

#include <algorithm>
#include <cassert>
#include <cmath>
#include <string_view>

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
    // Font size/face feed the multiline measure func exactly like TextNode's
    // (a stale measurement is the mis-sized-glyph class of bug); computed
    // against the CURRENT member props before the move.
    bool fontChanged = newProps.fontSize != props.fontSize || newProps.font != props.font;
    props = std::move(newProps);
    if (layoutChanged) {
        applyLayoutProps(props);
    }
    // Install/remove the measure func as the effective multiline mode toggles
    // (also covers a password toggle flipping the mode — password wins).
    syncMeasureFunc();
    if (fontChanged)
        markTextChanged();
    // Read displayText from the MOVED-IN member (props.value), not the moved-from
    // source (newProps.value is now empty).
    //
    // Controlled-model caret rule: the common case is the onChange ROUND-TRIP —
    // the app echoes the value we just reported straight back through props —
    // and the caret must NOT move then, or typing mid-string would snap it away
    // on every reconcile. Only a value that actually differs (an external
    // reset) replaces the text, clamping caret AND anchor into range — each
    // independently, so a selection survives where the new text allows — and
    // snapping backward to a UTF-8 code-point boundary (neither end ever sits
    // mid-code-point).
    if (props.value != displayText) {
        displayText = props.value;
        caret = utf8::snapToCodePoint(displayText, caret);
        selectionAnchor = utf8::snapToCodePoint(displayText, selectionAnchor);
        markTextChanged();
        verticalNavGoalX.reset();  // the goal column is meaningless in the new text
        // The replaced text's scroll is meaningless against the new run: drop
        // it, then re-follow the preserved caret into view. Without the
        // re-follow, a caret preserved deep in a now-LONGER value would sit
        // past the right clip edge (C1's pin is gone), invisible until the
        // next keypress. Not reached by the onChange echo (value ==
        // displayText), so ordinary typing round-trips keep their scroll. The
        // measurer is the same config-context one the geometry methods read
        // (TextNode::measureFunc idiom): available here because reconcile runs
        // against the host's config, on which setTextMeasurer installed it.
        textScrollX = 0;
        textScrollY = 0;
        scrollCaretIntoView(
            static_cast<const ITextMeasurer*>(YGConfigGetContext(YGNodeGetConfig(yogaNode))));
    }
}

namespace {

// The (size, face) an Input's text geometry measures with — the SAME
// resolution drawInput paints with (hover/focus styles may override the font
// size), so measured caret geometry and painted glyphs agree.
struct InputFont {
    float size;
    std::string_view face;
};

InputFont inputFont(const InputNode& n) {
    return {render::resolveInput(n.props, n.hovered, n.focused).fontSize,
            n.props.font ? std::string_view(*n.props.font) : std::string_view{}};
}

// Advance of one display-space run, falling back to the heuristic when no
// measurer is installed (matching TextNode::wrappedRuns).
float runWidth(const InputFont& f, std::string_view run, const ITextMeasurer* m) {
    return m ? m->measureRun(run, f.size, f.face) : fallbackMeasureRun(run, f.size, f.face);
}

// Line advance at the input's resolved font — the vertical unit of every
// multiline mapping (line index <-> y), same fallback rule as runWidth.
float inputLineHeight(const InputNode& n, const ITextMeasurer* m) {
    InputFont f = inputFont(n);
    return (m ? m->fontMetrics(f.size, f.face) : fallbackFontMetrics(f.size, f.face)).lineHeight;
}

}  // namespace

std::string InputNode::displayRun() const {
    // One '*' per CODE POINT, not per byte: a 2-byte 'é' is one star.
    if (props.password.value_or(false))
        return std::string(utf8::codePointCount(displayText), '*');
    return displayText;
}

float InputNode::prefixWidthAt(size_t i, const ITextMeasurer* m) const {
    std::string display = displayRun();
    std::string_view raw(displayText);
    size_t end = std::min(i, raw.size());
    // In star space a prefix of N code points is N one-byte stars.
    size_t dispEnd = props.password.value_or(false) ? utf8::codePointCount(raw.substr(0, end)) : end;
    return runWidth(inputFont(*this), std::string_view(display).substr(0, dispEnd), m);
}

size_t InputNode::indexAtPoint(float textX, const ITextMeasurer* m) const {
    InputFont font = inputFont(*this);
    bool masked = props.password.value_or(false);
    std::string display = displayRun();
    std::string_view disp(display);
    std::string_view raw(displayText);

    // Walk the raw code points, measuring the DISPLAY prefix through each.
    // The caret lands before the first code point whose midpoint textX does
    // not pass (ties resolve LEFT — see the header contract).
    float before = 0;
    size_t b = 0;   // byte offset into displayText (the returned space)
    size_t cp = 0;  // code-point index == star byte offset when masked
    while (b < raw.size()) {
        size_t next = utf8::nextCodePoint(raw, b);
        float after = runWidth(font, disp.substr(0, masked ? cp + 1 : next), m);
        if (textX <= (before + after) / 2)
            return b;
        before = after;
        b = next;
        ++cp;
    }
    return raw.size();
}

std::pair<size_t, size_t> InputNode::wordRangeAt(size_t i) const {
    std::string_view s(displayText);
    if (s.empty())
        return {0, 0};
    // The probed byte decides the run's class: the byte AT the boundary, or
    // for the end boundary the byte before it (the trailing run).
    size_t probe = std::min(i, s.size() - 1);
    bool space = s[probe] == ' ';
    size_t begin = probe;
    while (begin > 0 && (s[begin - 1] == ' ') == space)
        --begin;
    size_t end = probe;
    while (end < s.size() && (s[end] == ' ') == space)
        ++end;
    return {begin, end};
}

// --- InputNode: multiline geometry ---

float InputNode::wrapWidth() const {
    namespace rd = render_defaults;
    return std::max(0.0f, layout.contentWidth() - 2 * rd::kInputTextPad);
}

void InputNode::syncMeasureFunc() {
    if (multiline() && !YGNodeHasMeasureFunc(yogaNode)) {
        YGNodeSetContext(yogaNode, this);
        YGNodeSetMeasureFunc(yogaNode, &InputNode::measureFunc);
        YGNodeMarkDirty(yogaNode);  // measure under the new sizing model
    } else if (!multiline() && YGNodeHasMeasureFunc(yogaNode)) {
        // Dirty BEFORE removing (YGNodeMarkDirty asserts without a measure
        // func) so the next layout re-resolves the prop-driven size.
        YGNodeMarkDirty(yogaNode);
        YGNodeSetMeasureFunc(yogaNode, nullptr);
    }
}

YGSize InputNode::measureFunc(YGNodeConstRef node, float width, YGMeasureMode widthMode,
                              [[maybe_unused]] float height, [[maybe_unused]] YGMeasureMode heightMode) {
    // Mirror of TextNode::measureFunc, over displayText: the host's measurer is
    // recovered from the per-host Yoga config context, and reading through the
    // node's wrap cache keeps this, the edit-path geometry, and the tree
    // renderer on the SAME wrap. Installed only while multiline.
    namespace rd = render_defaults;
    auto* input = static_cast<InputNode*>(YGNodeGetContext(node));
    if (!input) {
        return {0, 0};
    }

    // The text draws kInputTextPad in from each side of the content box, so it
    // wraps at the available width minus both pads, and the pads are reported
    // back as part of the measured width.
    float maxWidth = widthMode == YGMeasureModeUndefined ? 0 : std::max(0.0f, width - 2 * rd::kInputTextPad);
    auto* measurer =
        static_cast<const ITextMeasurer*>(YGConfigGetContext(YGNodeGetConfig(const_cast<YGNodeRef>(node))));
    const auto& runs = input->displayRuns(maxWidth, measurer);
    Size size = render::runsSize(runs, inputLineHeight(*input, measurer));
    return {size.width + 2 * rd::kInputTextPad, size.height};
}

const std::vector<render::TextRun>& InputNode::displayRuns(float maxWidth, const ITextMeasurer* m) const {
    if (wrapCache_.valid && wrapCache_.measurer == m && wrapCache_.maxWidth == maxWidth) {
        return wrapCache_.runs;
    }
    InputFont font = inputFont(*this);
    wrapCache_.runs =
        render::wrapText(displayText, maxWidth, [&](std::string_view run) { return runWidth(font, run, m); });
    wrapCache_.measurer = m;
    wrapCache_.maxWidth = maxWidth;
    wrapCache_.valid = true;
    return wrapCache_.runs;
}

void InputNode::markTextChanged() {
    wrapCache_.valid = false;
    // Multiline only (the gate is the measure func itself): the measured line
    // count tracks the text, so an edit must re-measure at the next layout.
    if (YGNodeHasMeasureFunc(yogaNode))
        YGNodeMarkDirty(yogaNode);
}

size_t InputNode::lineOf(size_t i, const std::vector<render::TextRun>& runs) const {
    // First run whose range still reaches i: a caret exactly ON a soft-break
    // boundary (i == run.end == the next run's begin, minus dropped spaces)
    // resolves to the EARLIER line's end (soft-wrap affinity, documented on
    // caretPlacement). wrapText always yields at least one run.
    for (size_t k = 0; k < runs.size(); ++k) {
        if (i <= runs[k].end)
            return k;
    }
    return runs.size() - 1;
}

InputNode::CaretPlacement InputNode::caretPlacement(const ITextMeasurer* m) const {
    if (!multiline())
        return {0, caretPrefixWidth(m)};
    size_t line = lineOf(caret, displayRuns(wrapWidth(), m));
    return {line, prefixWidthInLine(line, caret, m)};
}

float InputNode::prefixWidthInLine(size_t line, size_t i, const ITextMeasurer* m) const {
    const auto& runs = displayRuns(wrapWidth(), m);
    const render::TextRun& run = runs[std::min(line, runs.size() - 1)];
    // Clamp into the run: a boundary inside a dropped soft-break space gap
    // maps to the line edge rather than a negative/overshot x.
    size_t inRun = std::clamp(i, run.begin, run.end);
    return runWidth(inputFont(*this), std::string_view(displayText).substr(run.begin, inRun - run.begin), m);
}

size_t InputNode::indexAtLineX(size_t line, float textX, const ITextMeasurer* m) const {
    const auto& runs = displayRuns(wrapWidth(), m);
    const render::TextRun& run = runs[std::min(line, runs.size() - 1)];
    // The single-line midpoint rule (see the 1-D indexAtPoint) over one line's
    // byte range, widths measured from the line's start — the same origin the
    // renderer draws the line from. Multiline text is never masked, so the
    // display prefix IS the raw prefix.
    InputFont font = inputFont(*this);
    std::string_view raw(displayText);
    float before = 0;
    size_t b = run.begin;
    while (b < run.end) {
        size_t next = utf8::nextCodePoint(raw, b);
        float after = runWidth(font, raw.substr(run.begin, next - run.begin), m);
        if (textX <= (before + after) / 2)
            return b;
        before = after;
        b = next;
    }
    return run.end;
}

size_t InputNode::indexAtPoint(float textX, float textY, const ITextMeasurer* m) const {
    if (!multiline())
        return indexAtPoint(textX, m);
    const auto& runs = displayRuns(wrapWidth(), m);
    float lineHeight = inputLineHeight(*this, m);
    // Clamp the y-derived line into range: a click above the text lands on
    // line 0, below it on the last line (then the x walk clamps within it).
    float rawLine = lineHeight > 0 ? std::floor(textY / lineHeight) : 0;
    size_t line = rawLine <= 0 ? 0 : std::min(static_cast<size_t>(rawLine), runs.size() - 1);
    return indexAtLineX(line, textX, m);
}

void InputNode::scrollCaretIntoView(const ITextMeasurer* m) {
    // The visible text span is the content box minus the text pad on BOTH
    // sides: text draws at the left pad, and the symmetric right pad keeps a
    // followed caret off the clip edge.
    float avail = wrapWidth();
    if (!multiline()) {
        float caretX = caretPrefixWidth(m);
        if (caretX - textScrollX < 0)
            textScrollX = caretX;  // caret left of the span: scroll left to it
        if (caretX - textScrollX > avail)
            textScrollX = caretX - avail;  // caret right of the span: scroll right
        // Never scroll past the text (deletions shrink it): dead space beyond the
        // last glyph would push the visible run needlessly left. Short text
        // clamps to 0, so an input whose text fits never scrolls at all.
        float total = runWidth(inputFont(*this), displayRun(), m);
        textScrollX = std::clamp(textScrollX, 0.0f, std::max(0.0f, total - avail));
        return;
    }

    // Multiline follows BOTH axes. X: the single-line follow within the
    // caret's LINE (wrapping keeps lines inside the span, so this stays ~0;
    // it matters only for an unbreakable run wider than the box).
    const auto& runs = displayRuns(avail, m);
    CaretPlacement place = caretPlacement(m);
    if (place.x - textScrollX < 0)
        textScrollX = place.x;
    if (place.x - textScrollX > avail)
        textScrollX = place.x - avail;
    textScrollX = std::clamp(textScrollX, 0.0f, std::max(0.0f, runs[place.line].width - avail));

    // Y: the textScrollX mirror — keep the caret's whole line box inside the
    // content height, clamped so no dead space opens past the last line. An
    // auto-grown input (measure-func height) always fits and stays at 0.
    // Gated on a laid-out viewport: before the first layout the content
    // height is 0 and the line-box test (which spans a whole lineHeight)
    // would scroll a line-0 caret off the top.
    float availY = layout.contentHeight();
    if (availY > 0) {
        float lineHeight = inputLineHeight(*this, m);
        float caretY = static_cast<float>(place.line) * lineHeight;
        if (caretY - textScrollY < 0)
            textScrollY = caretY;
        if (caretY + lineHeight - textScrollY > availY)
            textScrollY = caretY + lineHeight - availY;
        float totalY = static_cast<float>(runs.size()) * lineHeight;
        textScrollY = std::clamp(textScrollY, 0.0f, std::max(0.0f, totalY - availY));
    }
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

void ScrollNode::scrollTo(float x, float y) {
    targetScrollX = x;
    targetScrollY = y;
    clampScrollOffset();
}

void ScrollNode::scrollIntoView(const layout::Rect& target) {
    // The padded viewport in absolute space — the region content shows through.
    layout::Rect self = absoluteRect(this);
    float vx = self.x + layout.insetLeft;
    float vy = self.y + layout.insetTop;

    // The target in CONTENT space: its absolute rect was computed at the
    // CURRENT offsets, so adding them back yields scroll-independent coords.
    float cx = target.x - vx + scrollOffsetX;
    float cy = target.y - vy + scrollOffsetY;

    // Minimal scroll per axis, measured against the TARGET offsets so a call
    // during an in-flight animation composes with where the scroll is headed.
    // Far edge first, near edge second: for an oversized target the near
    // (top/left) alignment wins.
    if (cx + target.w > targetScrollX + layout.contentWidth())
        targetScrollX = cx + target.w - layout.contentWidth();
    if (cx < targetScrollX)
        targetScrollX = cx;
    if (cy + target.h > targetScrollY + layout.contentHeight())
        targetScrollY = cy + target.h - layout.contentHeight();
    if (cy < targetScrollY)
        targetScrollY = cy;
    clampScrollOffset();
}

ScrollbarGeometry ScrollNode::scrollbar(ScrollAxis axis) const {
    namespace rd = render_defaults;
    ScrollbarGeometry g;
    float vw = layout.contentWidth();
    float vh = layout.contentHeight();
    // A bar is active exactly when its axis overflows the padded viewport —
    // the same predicate the wheel path scrolls by and clampScrollOffset
    // ranges over. When both bars are active each track stops short of the
    // shared bottom-right corner.
    bool vActive = contentHeight > vh;
    bool hActive = contentWidth > vw;

    if (axis == ScrollAxis::Vertical) {
        float trackLen = vh - (hActive ? rd::kScrollbarThickness : 0.0f);
        if (!vActive || trackLen <= 0)
            return g;
        g.active = true;
        g.track = {layout.insetLeft + vw - rd::kScrollbarThickness, layout.insetTop, rd::kScrollbarThickness, trackLen};
        float thumbLen = std::min(trackLen, std::max(rd::kScrollbarMinThumbLen, trackLen * vh / contentHeight));
        float travel = trackLen - thumbLen;
        float maxScroll = contentHeight - vh;
        float pos = maxScroll > 0 ? (scrollOffsetY / maxScroll) * travel : 0.0f;
        g.thumb = {g.track.x, g.track.y + pos, rd::kScrollbarThickness, thumbLen};
    } else {
        float trackLen = vw - (vActive ? rd::kScrollbarThickness : 0.0f);
        if (!hActive || trackLen <= 0)
            return g;
        g.active = true;
        g.track = {layout.insetLeft, layout.insetTop + vh - rd::kScrollbarThickness, trackLen, rd::kScrollbarThickness};
        float thumbLen = std::min(trackLen, std::max(rd::kScrollbarMinThumbLen, trackLen * vw / contentWidth));
        float travel = trackLen - thumbLen;
        float maxScroll = contentWidth - vw;
        float pos = maxScroll > 0 ? (scrollOffsetX / maxScroll) * travel : 0.0f;
        g.thumb = {g.track.x + pos, g.track.y, thumbLen, rd::kScrollbarThickness};
    }
    return g;
}

ScrollbarPart ScrollNode::scrollbarHitTest(float localX, float localY) const {
    auto contains = [](const layout::Rect& r, float px, float py) {
        return px >= r.x && px < r.right() && py >= r.y && py < r.bottom();
    };
    ScrollbarGeometry v = scrollbar(ScrollAxis::Vertical);
    if (v.active && contains(v.track, localX, localY))
        return contains(v.thumb, localX, localY) ? ScrollbarPart::VerticalThumb : ScrollbarPart::VerticalTrack;
    ScrollbarGeometry h = scrollbar(ScrollAxis::Horizontal);
    if (h.active && contains(h.track, localX, localY))
        return contains(h.thumb, localX, localY) ? ScrollbarPart::HorizontalThumb : ScrollbarPart::HorizontalTrack;
    return ScrollbarPart::None;
}

float ScrollNode::scrollPerThumbPixel(ScrollAxis axis) const {
    ScrollbarGeometry g = scrollbar(axis);
    if (!g.active)
        return 0.0f;
    bool vertical = axis == ScrollAxis::Vertical;
    float travel = vertical ? g.track.h - g.thumb.h : g.track.w - g.thumb.w;
    if (travel <= 0)
        return 0.0f;
    float maxScroll = vertical ? contentHeight - layout.contentHeight() : contentWidth - layout.contentWidth();
    return maxScroll / travel;
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
