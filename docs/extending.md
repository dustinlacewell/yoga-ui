# Extending Primitives

Most UI is composition of Box + Text + Input. Only add new primitives when truly needed.

## When to Extend

Add a primitive when you need:
- Custom drawing (Canvas for arbitrary graphics)
- Special input handling (Slider, Knob)
- Performance optimization (virtualized list)

If it can be composed from existing primitives, make it a component instead.

## How to Extend

### 1. Add to PrimitiveType enum (VNode.hpp)

```cpp
enum class PrimitiveType { Box, Text, Input, Canvas };
```

### 2. Define props struct (Props.hpp)

Props structs inherit from `LayoutProps` and `EventProps`:

```cpp
struct CanvasProps : LayoutProps, EventProps {
    std::function<void(NVGcontext*, float w, float h)> draw;
    // Add state-based style overrides if needed
    std::optional<CanvasStyle> hoverStyle;
    std::optional<CanvasStyle> focusStyle;
};

// Add to PropsVariant
using PropsVariant = std::variant<BoxProps, TextProps, InputProps, CanvasProps>;
```

### 3. Create VNode factory (VNode.hpp)

```cpp
inline VNode Canvas(std::function<void(NVGcontext*, float, float)> drawFn) {
    VNode node;
    node.type = PrimitiveType::Canvas;
    CanvasProps p;
    p.draw = std::move(drawFn);
    node.props = std::move(p);
    return node;
}
```

### 4. Implement Node subclass (Node.hpp/cpp)

```cpp
class CanvasNode : public Node {
public:
    CanvasNode();
    PrimitiveType type() const override { return PrimitiveType::Canvas; }
    void updateProps(const PropsVariant& props) override;

    CanvasProps props;
};
```

In `updateProps`, extract props and call `applyLayoutProps()`:

```cpp
void CanvasNode::updateProps(const PropsVariant& p) {
    props = std::get<CanvasProps>(p);
    applyLayoutProps(props);  // Apply layout to Yoga node
}
```

### 5. Update createNode factory (Node.cpp)

```cpp
std::unique_ptr<Node> createNode(PrimitiveType type) {
    switch (type) {
        // ...existing cases...
        case PrimitiveType::Canvas: return std::make_unique<CanvasNode>();
    }
}
```

### 6. Add to renderer (e.g., NvgRenderer.cpp)

```cpp
void NvgRenderer::drawCanvas(DrawContext& ctx, CanvasNode* node) {
    if (node->props.draw) {
        node->props.draw(ctx.vg, node->layout.width, node->layout.height);
    }
}
```

Add a case in `NvgRenderer::draw()` to handle the new node type.
