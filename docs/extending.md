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
enum class PrimitiveType { Box, Text, Input, Scroll, Canvas, Portal };
```

### 2. Define props struct (Props.hpp)

Props structs inherit from `LayoutProps` and `EventProps`:

```cpp
struct CanvasProps : LayoutProps, EventProps {
    std::function<void(void*, float w, float h)> draw;
    // Add state-based style overrides if needed
    std::optional<CanvasStyle> hoverStyle;
    std::optional<CanvasStyle> focusStyle;
};

// Add to PropsVariant
using PropsVariant = std::variant<BoxProps, TextProps, InputProps, ScrollProps, CanvasProps, PortalProps>;
```

### 3. Create VNode factory (VNode.hpp)

```cpp
inline VNode Canvas(CanvasDrawFn drawFn) {
    VNode n;
    CanvasProps p;
    p.draw = std::move(drawFn);
    n.props = std::move(p);  // VNode::type() derives from props.index()
    return n;
}
```

The primitive's position in `PropsVariant` *is* its `PrimitiveType` (a `static_assert`
in `VNode.hpp` keeps the enum and the variant in the same order), so setting `props`
is all that's needed — `n.type()` reports `Canvas` automatically.

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

New primitives automatically participate in the dual-tree architecture. The Reconciler creates a Host fiber and corresponding render Node for each instance. No Fiber-specific code is needed — fibers only require special handling for Components.

### 5. Update createNode factory (Node.cpp)

```cpp
std::unique_ptr<Node> createNode(PrimitiveType type) {
    switch (type) {
        // ...existing cases...
        case PrimitiveType::Canvas: return std::make_unique<CanvasNode>();
    }
}
```

### 6. Add to the render walk (src/render/TreeRenderer.cpp)

Since 1.1, drawing is **not** per-backend — a new primitive's paint logic goes
into the one backend-neutral walk, `render::renderTree`, which every backend
shares (see [Architecture — Rendering](architecture.md#rendering)). Add a
case to the node-type switch there, expressed only in terms of the
`IRenderBackend` primitives (`fillRect`, `strokeRect`, `pushClip`/`popClip`,
`drawTextRun`, `drawCanvas`):

```cpp
// in the renderTree node-type switch:
case PrimitiveType::MyPrimitive: {
    auto* n = static_cast<MyPrimitiveNode*>(node);
    backend.fillRect(absoluteRectOf(n), n->props.backgroundColor.value_or(0), 0);
    // ...
    break;
}
```

Do **not** add a per-backend draw method (e.g. `NvgRenderer::drawMyPrimitive`)
unless the primitive needs something no `IRenderBackend` primitive can
express — that would reintroduce exactly the per-backend duplication the
render seam was built to eliminate. `Canvas` is the one primitive that
legitimately needs backend-specific handling (the user's draw callback
receives an opaque, backend-specific context), which is why
`IRenderBackend::drawCanvas` exists as a dedicated virtual — most new
primitives should need nothing beyond the shared fill/stroke/clip/text
surface.
