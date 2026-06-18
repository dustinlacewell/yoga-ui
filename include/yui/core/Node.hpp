#pragma once

#include "Measure.hpp"
#include "Props.hpp"
#include "VNode.hpp"

#include <memory>
#include <vector>

#include <yoga/Yoga.h>

namespace yui {

// Computed layout result from Yoga
struct LayoutResult {
    float left = 0;
    float top = 0;
    float width = 0;
    float height = 0;
};

// Base class for all rendered nodes
class Node {
public:
    virtual ~Node();

    // Key constants (must match VNode)
    static constexpr int64_t NO_INT_KEY = INT64_MIN;
    static constexpr size_t NO_SOURCE_POSITION = SIZE_MAX;

    // Identity
    virtual PrimitiveType type() const = 0;
    std::string key;
    int64_t intKey = NO_INT_KEY;
    size_t sourcePosition = NO_SOURCE_POSITION;

    // Key helpers
    bool hasKey() const { return intKey != NO_INT_KEY || !key.empty(); }
    bool hasIntKey() const { return intKey != NO_INT_KEY; }
    bool hasStringKey() const { return !key.empty(); }

    // Props update (called by reconciler)
    virtual void updateProps(const PropsVariant& props) = 0;

    // Tree structure
    std::vector<std::unique_ptr<Node>> children;
    Node* parent = nullptr;

    // Layout — every node has a valid yogaNode
    YGNodeRef yogaNode = nullptr;
    LayoutResult layout;

    // Interactive state (updated by EventHandler)
    bool hovered = false;
    bool focused = false;

    // Apply layout props to yoga node
    void applyLayoutProps(const LayoutProps& props);

    // Sync layout results from yoga after calculation
    void syncLayoutFromYoga();

    // Calculate layout for this subtree
    void calculateLayout(float availableWidth, float availableHeight);

    // Update animations (smooth scrolling, etc). Returns true if any animation is active.
    bool update(float dt);

protected:
    // The yoga node is created against the host's config so its measure
    // callback can recover the host's text measurer from the config context.
    explicit Node(YGConfigRef config);
};

// Concrete node types
class BoxNode : public Node {
public:
    explicit BoxNode(YGConfigRef config);
    PrimitiveType type() const override { return PrimitiveType::Box; }
    void updateProps(const PropsVariant& props) override;

    BoxProps props;
};

class TextNode : public Node {
public:
    explicit TextNode(YGConfigRef config);
    PrimitiveType type() const override { return PrimitiveType::Text; }
    void updateProps(const PropsVariant& props) override;

    TextProps props;

private:
    void setupMeasureFunc();
    static YGSize measureFunc(YGNodeConstRef node, float width, YGMeasureMode widthMode, float height,
                              YGMeasureMode heightMode);
};

class InputNode : public Node {
public:
    explicit InputNode(YGConfigRef config);
    PrimitiveType type() const override { return PrimitiveType::Input; }
    void updateProps(const PropsVariant& props) override;

    InputProps props;

    // Display text - synced from props.value, modified during editing
    std::string displayText;
};

class ScrollNode : public Node {
public:
    explicit ScrollNode(YGConfigRef config);
    PrimitiveType type() const override { return PrimitiveType::Scroll; }
    void updateProps(const PropsVariant& props) override;

    ScrollProps props;

    // Scroll state
    float targetScrollX = 0;
    float targetScrollY = 0;
    float scrollOffsetX = 0;
    float scrollOffsetY = 0;

    // Content size (computed after layout)
    float contentWidth = 0;
    float contentHeight = 0;

    void updateContentSize();
    void clampScrollOffset();
    bool updateSmooth(float dt);
};

class CanvasNode : public Node {
public:
    explicit CanvasNode(YGConfigRef config);
    PrimitiveType type() const override { return PrimitiveType::Canvas; }
    void updateProps(const PropsVariant& props) override;

    CanvasProps props;
};

// Factory: create Node from VNode type. The yoga node is created against the
// given config (may be nullptr, which resolves to Yoga's default config).
std::unique_ptr<Node> createNode(PrimitiveType type, YGConfigRef config);

}  // namespace yui
