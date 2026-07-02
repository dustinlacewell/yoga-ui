#pragma once

#include "Measure.hpp"
#include "Props.hpp"
#include "VNode.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <yoga/Yoga.h>

namespace yui {

// Upper bound on tree depth for the recursive structural walks (event dispatch,
// hit testing, key-target search). These descend/bubble one native stack frame
// per tree level; a pathologically deep, data-driven tree could otherwise
// overflow the stack — a crash reachable from the public input API. 1024 is far
// beyond any realistic UI nesting yet a small fraction of the few-thousand-frame
// budget a native stack affords these tiny functions, so it diagnoses the
// degenerate case via the error sink long before the stack is at risk. The
// reconciler's VNode walk is NOT guarded here (aborting mid-reconcile would leave
// the tree partially built); its depth bound is documented as a precondition on
// the render API (see Host::setRender).
constexpr int kMaxTreeDepth = 1024;

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

    // Props update (called by reconciler). Takes the new props by rvalue: the
    // VNode is fully consumable after reconcile (nothing reads it later), so the
    // owned props are MOVED into the node instead of copied.
    virtual void updateProps(PropsVariant&& props) = 0;

    // Tree structure
    std::vector<std::unique_ptr<Node>> children;
    Node* parent = nullptr;

    // Layout — every node has a valid yogaNode
    YGNodeRef yogaNode = nullptr;
    LayoutResult layout;

    // Interactive state (updated by EventHandler)
    bool hovered = false;
    bool focused = false;

    // Liveness token shared with holders that outlive this node — notably the
    // EventHandler's focusedInput_, which keeps a raw InputNode* that a
    // reconciliation may free out from under it. Cleared in ~Node so those
    // holders observe a dead token and treat the pointer as gone instead of
    // dereferencing freed memory. Mirrors the Fiber/Store/Host alive_ idiom:
    // observe via a weak_ptr copy, verify liveness before touching the node.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

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
    void updateProps(PropsVariant&& props) override;

    BoxProps props;
};

class TextNode : public Node {
public:
    explicit TextNode(YGConfigRef config);
    PrimitiveType type() const override { return PrimitiveType::Text; }
    void updateProps(PropsVariant&& props) override;

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
    void updateProps(PropsVariant&& props) override;

    InputProps props;

    // Display text - synced from props.value, modified during editing
    std::string displayText;
};

class ScrollNode : public Node {
public:
    explicit ScrollNode(YGConfigRef config);
    PrimitiveType type() const override { return PrimitiveType::Scroll; }
    void updateProps(PropsVariant&& props) override;

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
    void updateProps(PropsVariant&& props) override;

    CanvasProps props;
};

// Factory: create Node from VNode type. The yoga node is created against the
// given config (may be nullptr, which resolves to Yoga's default config).
std::unique_ptr<Node> createNode(PrimitiveType type, YGConfigRef config);

}  // namespace yui
