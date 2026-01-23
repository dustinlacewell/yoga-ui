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

    // Lifecycle
    virtual void willUnmount() {}

    // Tree structure
    std::vector<std::unique_ptr<Node>> children;
    Node* parent = nullptr;

    // Layout
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
    Node();
};

// Concrete node types
class BoxNode : public Node {
public:
    BoxNode();
    PrimitiveType type() const override { return PrimitiveType::Box; }
    void updateProps(const PropsVariant& props) override;

    BoxProps props;
};

class TextNode : public Node {
public:
    TextNode();
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
    InputNode();
    PrimitiveType type() const override { return PrimitiveType::Input; }
    void updateProps(const PropsVariant& props) override;

    InputProps props;
};

class ScrollNode : public Node {
public:
    ScrollNode();
    PrimitiveType type() const override { return PrimitiveType::Scroll; }
    void updateProps(const PropsVariant& props) override;

    ScrollProps props;

    // Scroll state - target is where we want to be, current is interpolated
    float targetScrollX = 0;
    float targetScrollY = 0;
    float scrollOffsetX = 0;
    float scrollOffsetY = 0;

    // Content size (computed after layout)
    float contentWidth = 0;
    float contentHeight = 0;

    // Sync content size from children's current layout
    void updateContentSize();

    // Clamp scroll offset to valid range
    void clampScrollOffset();

    // Update smooth scrolling (call each frame with delta time)
    // Returns true if still animating
    bool updateSmooth(float dt);
};

class CanvasNode : public Node {
public:
    CanvasNode();
    PrimitiveType type() const override { return PrimitiveType::Canvas; }
    void updateProps(const PropsVariant& props) override;

    CanvasProps props;
};

// Factory: create Node from VNode type
std::unique_ptr<Node> createNode(PrimitiveType type);

}  // namespace yui
