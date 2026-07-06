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
| `Portal` | Detached-content container (root z-order, viewport layout) |

Each primitive has:
- A `VNode` factory function (creates lightweight descriptions)
- A `Node` class implementation (persistent instance with layout and state)

All primitives support:
- Yoga layout props (width, height, flex, padding, margin, gap, etc.)
- Visual style props (backgroundColor, borderColor, etc.) — except `Portal`, which is pure plumbing with none
- State-based style overrides (hoverStyle, focusStyle)
- Event handlers (onClick, onRightClick, onMiddleClick, onDoubleClick, onHover, onHoverDelay, onFocus, onMouseDown/Up/Move, onDrag)
- Focus (`.focusable()`, `.autoFocus()`)
- Keys for reconciliation (`.setKey("id")`)

`Portal` is architecturally distinct from the other five: its content
*reconciles* in its logical parent (state/hooks/refs stay put — no
reparenting) but *renders* detached — laid out against the viewport instead
of its parent's box, and painted/hit-tested at root z-order, escaping any
ancestor's clip. It's the one seam floating UI (dropdowns, dialogs,
tooltips) is built on; see [Overlays](overlays.md).

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

Component functions must be const-callable (no mutable lambdas), enforced at compile time — see [Components](components.md#stateful-components) for why.

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

The `EventHandler` class manages hover, focus, and pointer-capture state on
the render tree:
- Tracks `hoveredNode_` and `focusedInput_` (more generally, the focused
  `Node*`) pointers
- Updates `node->hovered` and `node->focused` flags
- Calls `onHover`/`onFocus` callbacks
- Receives removal notifications from Reconciler to clear stale pointers

When nodes are removed during reconciliation, the Reconciler notifies EventHandler via a callback so it can clear any references to those nodes.

### Pointer capture

The node a press (`onMouseDown`) lands on becomes the **implicit captor**
for that gesture: while any button stays held, `onMouseMove` is routed to
the captor even if the pointer leaves its bounds (or the window), and the
matching `onMouseUp` is delivered to the captor wherever the release
actually lands — not wherever is currently under the pointer. `onClick`
requires the press *and* release to resolve to the same handler-bearing
node; a release outside that node's subtree (dragged away, or covered by
something else mid-press) fires no click on either side. `onDrag` is a
derived signal on top of the same capture: once the pointer has moved past
`render_defaults::kDragThresholdPx` from the press anchor, capture stops
delivering plain moves and starts delivering `DragEvent`s (current position,
delta since the last one, and the original press anchor). This is the
mechanism [`Slider`](widgets.md#slider) and draggable `Scroll` scrollbars
are both built on — see [Primitives — pointer capture and
drag](primitives.md#pointer-capture-and-drag).

### Focus and traversal

Beyond a single `focusedInput_`, focus generalizes to any node with
`.focusable(true)` (an `Input` is always focusable) or `.autoFocus(true)`
(focused on mount, regardless of `.focusable()` — a `tabindex="-1"`-style
programmatic target). `Host::focusNext()`/`focusPrev()` walk the focusables
in document order, wrapping, and can be scoped to a subtree via
`Host::setFocusTrap(node)` — the mechanism [`Modal`](overlays.md#modal) uses
(through `Portal::trapFocus()`) to keep Tab inside an open dialog and
restore whatever was focused before it opened. A focus trap whose root node
is removed by a later reconcile un-scopes itself silently (the same
liveness-token discipline as `Fiber`/`Store`/`Node::alive`), rather than
leaving traversal stuck pointing at a dead subtree.

## Rendering

Rendering is backend-neutral: everything above raw fill/stroke/clip/text
primitives — the node-type switch, style cascade, text wrapping, scroll
clip/offset math, input chrome (caret, selection highlight), and portal
paint order — lives in one shared walk, `render::renderTree(Node* root,
IRenderBackend& backend, const ErrorHandler& onError)`, identical across
every backend by construction. A backend implements only:

```cpp
class IRenderBackend : public ITextMeasurer {
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void fillRect(const Rect&, uint32_t color, float radius) = 0;
    virtual void strokeRect(const Rect&, uint32_t color, float radius, float width) = 0;
    virtual void pushClip(const Rect&, float radius) = 0;
    virtual void popClip() = 0;
    virtual void drawTextRun(const std::string& run, float x, float y,
                              float fontSize, uint32_t color, std::string_view font) = 0;
    virtual void drawCanvas(const CanvasNode&, const Rect&) = 0;
};
```

`Rect` here is always absolute (root-relative) — the walk resolves every
node to absolute space before calling the backend, so a backend never tracks
its own offsets or translations. Because `IRenderBackend` inherits
`ITextMeasurer`, one object serves as both the renderer and the text
measurer a `Host` installs via `setTextMeasurer` — measure and paint are
guaranteed to agree because they're the same face lookup
(`measureRun`/`fontMetrics`, both keyed by font **name**, resolved to a
backend handle without global mutable state).

A frame is bracketed by the caller: `backend.beginFrame(); renderTree(root,
backend, onError); backend.endFrame();`. The walk is `noexcept` — rendering
runs inside the platform's draw callback, a C boundary a C++ exception must
never cross, so anything a callback throws (including a user `Canvas` draw
callback, isolated per-node) is routed to `onError` instead, and
`endFrame()` restores whatever backend state the walk left unbalanced (a
half-popped clip stack, etc.), so an embedder can safely interleave its own
drawing around a `render()` call without re-asserting state.

The bundled `NvgRenderer` (NanoVG) is the reference implementation; `SdlRenderer`
(SDL2 + SDL2_gfx) is the other shipped backend. The actual walk and its
supporting pieces live in `src/render/{TreeRenderer,StyleResolver,TextWrap,Measure}.cpp`
— read those alongside a bundled renderer if you're implementing a new
backend; see also [Extending](extending.md).
