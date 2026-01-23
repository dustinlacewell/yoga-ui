# Architecture

## Two Layers

### Layer 1: Primitives (Library Provides)

A small, fixed set of primitive node types:

| Primitive | Purpose |
|-----------|---------|
| `Box` | Layout container (like `div`) |
| `Text` | Text display |
| `Input` | Text input field |

Each primitive has:
- A `VNode` factory function (creates descriptions)
- A `Node` class implementation (handles rendering/events)

All primitives support:
- Yoga layout props (width, height, flex, padding, margin, gap, etc.)
- Visual style props (backgroundColor, borderColor, etc.)
- State-based style overrides (hoverStyle, focusStyle)
- Event handlers (onClick, onRightClick, onHover, onFocus)
- Keys for reconciliation (`.setKey("id")`)

### Layer 2: Components (You Write)

Functions that compose primitives. No registration needed.

```cpp
VNode Button(std::string label, std::function<void()> onClick) {
    return Box({
        Text(label).color(WHITE)
    })
    .padding(8)
    .backgroundColor(BLUE)
    .onClick(onClick);
}
```

**No registration, no string mapping.** Components are just functions.

## VNode vs Node

**VNode** - Lightweight value describing desired UI:
```cpp
struct VNode {
    PrimitiveType type;
    std::string key;
    PropsVariant props;
    std::vector<VNode> children;
    bool isEmpty = false;  // For conditional rendering

    // Fluent API
    VNode& width(float v);
    VNode& flexGrow(float v);
    VNode& setKey(std::string k);
    // ...
};
```

**Node** - Actual widget instance with state:
```cpp
class Node {
public:
    virtual PrimitiveType type() const = 0;
    std::string key;
    size_t sourcePosition = SIZE_MAX;  // VNode index for position matching

    // Lifecycle
    virtual void updateProps(const PropsVariant& props) = 0;
    virtual void willUnmount() {}

    // Tree
    std::vector<std::unique_ptr<Node>> children;
    Node* parent = nullptr;

    // Layout
    YGNodeRef yogaNode = nullptr;
    LayoutResult layout;

    // Interactive state (updated by EventHandler)
    bool hovered = false;
    bool focused = false;
};
```

Renderers use `hovered`/`focused` flags to apply state-based style overrides.

## Data Flow

```
┌─────────────────────────────────────────────────────────────┐
│                        Each Frame                           │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   State ──────► render() ──────► VNode Tree (description)   │
│                                         │                   │
│                                         ▼                   │
│                               ┌─────────────────┐           │
│   Actual Node Tree ◄──────────│   Reconciler    │           │
│   (persistent, stateful)      └─────────────────┘           │
│         │                                                   │
│         ▼                                                   │
│   Layout (Yoga) ──► Draw ──► Events                         │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

The reconciler:
- **Reuses** existing nodes where possible (preserving hover/focus state)
- **Creates** new nodes where needed
- **Removes** nodes that no longer exist (calls `willUnmount()`, notifies EventHandler)
- **Updates** props on reused nodes

## Event Handling

The `EventHandler` class manages hover and focus state:
- Tracks `hoveredNode_` and `focusedInput_` pointers
- Updates `node->hovered` and `node->focused` flags
- Calls `onHover`/`onFocus` callbacks
- Receives removal notifications from Reconciler to clear stale pointers

When nodes are removed during reconciliation, the Reconciler notifies EventHandler via a callback so it can clear any references to those nodes.
