# Architecture

## Two Layers

### Layer 1: Primitives (Library Provides)

A small, fixed set of primitive node types:

| Primitive | Purpose |
|-----------|---------|
| `Box` | Layout container (like `div`) |
| `Text` | Text display |
| `Input` | Text input field |
| `Scroll` | Scrollable container with clipping |
| `Canvas` | Custom drawing surface |

Each primitive has:
- A `VNode` factory function (creates lightweight descriptions)
- A `Node` class implementation (persistent instance with layout and state)

All primitives support:
- Yoga layout props (width, height, flex, padding, margin, gap, etc.)
- Visual style props (backgroundColor, borderColor, etc.)
- State-based style overrides (hoverStyle, focusStyle)
- Event handlers (onClick, onRightClick, onHover, onFocus)
- Keys for reconciliation (`.setKey("id")`)

### Layer 2: Components (You Write)

There are two kinds of components:

**Helper functions** compose primitives into reusable VNode trees. No registration needed:

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

**Stateful components** use `Component()` to create a deferred render function with access to hooks:

```cpp
Component Counter() {
    return [](ComponentContext& ctx) -> VNode {
        auto [count, setCount] = ctx.useState(0);

        return Row({
            Text(std::to_string(count)),
            Box(Text("+")).onClick([=] { setCount(count + 1); }),
        }).gap(8);
    };
}
```

Stateful components are first-class children — mix them freely with VNodes:

```cpp
Box({
    Text("Header"),        // VNode
    Counter(),             // Component
    Text("Footer"),        // VNode
})
```

## Three Core Types

### VNode — Lightweight Description

Created fresh every render. Describes *what* the UI should look like:

```cpp
struct VNode {
    PrimitiveType type;
    std::string key;
    PropsVariant props;
    std::vector<Child> children;  // Child = variant<VNode, Component>
    bool isEmpty = false;         // For conditional rendering

    // Fluent API
    VNode& width(float v);
    VNode& flexGrow(float v);
    VNode& setKey(std::string k);
    // ...
};
```

### Component — Deferred Render Function

Wraps a function that receives `ComponentContext&` and returns a `VNode`. The reconciler calls this function during mount and re-render:

```cpp
struct Component {
    ComponentFn fn;  // std::function<VNode(ComponentContext&)>
    std::string key;

    // Constructed from any const-callable
    template<typename F> Component(F f);

    Component& setKey(std::string k);
    Component& setKey(int64_t k);
};
```

**Note:** Component functions must be const-callable (no mutable lambdas). This is enforced at compile time — mutable lambdas that capture-and-move are a re-render correctness hazard.

### Node — Persistent Widget Instance

Actual rendered widget. Persists across frames. Holds computed layout and interactive state:

```cpp
class Node {
public:
    virtual PrimitiveType type() const = 0;
    std::string key;
    size_t sourcePosition = SIZE_MAX;

    // Props update (called by Reconciler)
    virtual void updateProps(const PropsVariant& props) = 0;

    // Tree
    std::vector<std::unique_ptr<Node>> children;
    Node* parent = nullptr;

    // Layout (Yoga)
    YGNodeRef yogaNode = nullptr;
    LayoutResult layout;

    // Interactive state (updated by EventHandler)
    bool hovered = false;
    bool focused = false;
};
```

Renderers use `hovered`/`focused` flags to apply state-based style overrides.

## Dual-Tree Architecture

The Reconciler maintains two parallel trees:

### Fiber Tree (Reconciliation + Component State)

```cpp
struct Fiber {
    enum class Tag { Host, Component };
    Tag tag;

    // Tree structure
    Fiber* parent;
    std::vector<std::unique_ptr<Fiber>> children;

    // Host fibers point to a render node
    Node* renderNode = nullptr;

    // Component fibers hold state and hooks
    ComponentFn componentFn;
    std::vector<std::any> hookState;        // useState, useRef storage
    std::vector<SubscriptionRecord> subscriptionCleanups;  // {store, resubscribe, unsubscribe}
    std::vector<std::function<void()>> effectCleanups;
    bool dirty = false;
};
```

- **Host fibers** (Tag::Host) correspond 1:1 with render Nodes
- **Component fibers** (Tag::Component) hold hook state and subscriptions. They are *transparent* in the render tree — their output nodes are parented to the nearest host ancestor

### Render Tree (Layout + Drawing + Events)

The Node tree described above. Owns Yoga layout nodes and is consumed directly by renderers and the EventHandler. Components do not appear in the render tree.

### Why Two Trees?

- **Fiber tree** tracks component identity, hook state, and Store subscriptions across re-renders
- **Render tree** is a flat primitive-only tree optimized for layout calculation and hit testing
- Components can re-render independently (selective dirty reconciliation) without rebuilding the entire render tree

## Data Flow

```
┌──────────────────────────────────────────────────────────────────────┐
│                           Each Frame                                 │
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   Store.set() ───► marks fibers dirty (selective)                    │
│                    or host dirty (full re-render)                    │
│                                                                      │
│   Host::update()                                                     │
│     1. reconcileDirtyComponents() ─► walk fiber tree                 │
│        └─ re-render only dirty components                            │
│        └─ reconcile their subtrees in the render tree                │
│                                                                      │
│     2. Full reconcile (if host dirty):                               │
│        render() ──► VNode tree ──► Reconciler                        │
│                                     ├─► Fiber tree (updated)         │
│                                     └─► Render tree (updated)        │
│                                                                      │
│     3. Layout (Yoga) on render tree                                  │
│     4. Renderer draws render tree                                    │
│     5. EventHandler dispatches to render tree nodes                  │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

The reconciler:
- **Reuses** existing nodes where possible (preserving hover/focus state)
- **Creates** new nodes where needed
- **Removes** nodes that no longer exist (runs fiber cleanups, notifies EventHandler)
- **Updates** props on reused nodes

## ComponentContext and Hooks

Component functions receive a `ComponentContext&` providing React-like hooks:

| Hook | Purpose |
|------|---------|
| `useState<T>(initial)` | Local state with setter that triggers re-render |
| `useRef<T>(initial)` | Mutable storage that persists across renders (no re-render) |
| `useEffect(fn)` | Side effects after render, with optional cleanup |
| `useField(store, &S::field)` | Two-way binding to a Store field |

Hook state is stored on the Fiber and persists across re-renders. Hooks must be called in the same order every render (same rule as React).

## Store and Selective Re-rendering

`Store<T>` is a reactive state container. When `use()` is called:
- **Inside a component**: subscribes only that component's fiber. On `set()`, only that component re-renders.
- **In the top-level render function**: subscribes the whole host. On `set()`, the entire tree re-renders.

This means a Store change can re-render a single component in a tree of thousands — the rest of the fiber/render tree is untouched.

## Event Handling

The `EventHandler` class manages hover and focus state on the render tree:
- Tracks `hoveredNode_` and `focusedInput_` pointers
- Updates `node->hovered` and `node->focused` flags
- Calls `onHover`/`onFocus` callbacks
- Receives removal notifications from Reconciler to clear stale pointers

When nodes are removed during reconciliation, the Reconciler notifies EventHandler via a callback so it can clear any references to those nodes.
