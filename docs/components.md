# Components

## Helper Functions (Stateless)

The simplest way to compose UI. Just functions that return VNodes:

```cpp
VNode Button(std::string label, std::function<void()> onClick) {
    return Box({
        Text(label).color(WHITE)
    })
    .padding(8)
    .backgroundColor(BLUE)
    .onClick(onClick);
}

VNode LabeledField(std::string label, VNode field) {
    return Column({
        Text(label).color(DIM).fontSize(10),
        field,
    }).gap(2);
}
```

Helper functions are evaluated immediately and produce VNode subtrees. They have no identity in the fiber tree — the reconciler sees their output directly.

## Stateful Components

For local state, lifecycle effects, or selective re-rendering, wrap a lambda in `Component()`:

```cpp
Component Counter() {
    return [](ComponentContext& ctx) -> VNode {
        auto [count, setCount] = ctx.useState(0);

        return Row({
            Text("Count: " + std::to_string(count)),
            Box(Text("+"))
                .padding(8)
                .onClick([=] { setCount(count + 1); }),
        }).gap(8);
    };
}
```

Components are first-class children — mix them with VNodes in any container:

```cpp
Column({
    Text("Header"),        // VNode
    Counter(),             // Component
    Text("Footer"),        // VNode
})
```

The component function is called by the reconciler during mount and whenever the component is marked dirty. It receives a `ComponentContext&` for accessing hooks.

**Important:** Component functions must be const-callable. Mutable lambdas (e.g., those that `std::move` captured values) are rejected at compile time — component functions may be called multiple times across re-renders.

## Hooks

### useState

Local state that persists across re-renders. The setter triggers a re-render of just this component:

```cpp
auto [count, setCount] = ctx.useState(0);
auto [name, setName] = ctx.useState<std::string>("Alice");

// Replace value
setCount(42);

// Multiple useState calls are fine
auto [a, setA] = ctx.useState(0);
auto [b, setB] = ctx.useState("");
```

### useRef

Mutable storage that persists across renders without triggering re-renders:

```cpp
int& renderCount = ctx.useRef<int>(0);
renderCount++;  // Survives across re-renders, does NOT trigger re-render
```

### useEffect

Run side effects after mount. Return a cleanup function (or nullptr):

```cpp
ctx.useEffect([&]() {
    // Runs after mount
    startTimer();
    return [&]() {
        // Cleanup runs on unmount
        stopTimer();
    };
});

ctx.useEffect([&]() {
    log("mounted");
    return nullptr;  // No cleanup needed
});
```

Effects run in order. Cleanup functions run when the component unmounts.

### useField

Two-way binding to a specific field of a Store. Subscribes only to changes affecting that Store:

```cpp
struct FormState {
    std::string username;
    int age;
};

Store<FormState> formStore(FormState{"", 0});

auto FormComponent = [&](ComponentContext& ctx) -> VNode {
    auto [username, setUsername] = ctx.useField(formStore, &FormState::username);
    auto [age, setAge] = ctx.useField(formStore, &FormState::age);

    return Column({
        Input().value(username).onChange([=](const std::string& v) { setUsername(v); }),
        Text(username + " is " + std::to_string(age)),
    });
};
```

### useElementRef

A stable handle to a host element's rendered node, so you can read its drawn
position after layout — the analog of React's `useRef` + `<div ref={ref}>` +
`ref.current`. Attach it to a host element with `.ref(...)`, then read the node
in an event handler (never during render):

```cpp
auto Menu = [&](ComponentContext& ctx) -> VNode {
    auto rowRef = ctx.useElementRef();         // stable across re-renders
    return Row({ Text("File"), Text("\xe2\x96\xb8") })
        .ref(rowRef)                           // attaches to the Row (a host element)
        .onHover([rowRef](bool over) {
            if (!over) return;
            Node* row = rowRef.current();       // nullptr if reconciled away — guard it
            if (!row) return;
            Rect r = rowRef.getBoundingRect();  // the row's absolute drawn rect
            openSubmenuAt(r);
        });
};
```

- `current()` (alias `get()`) returns the live `Node*`, or `nullptr` — both when
  the element has been reconciled away **and during render** (like React, refs
  are null during render; read them only in handlers/effects against settled
  layout). Always guard the result.
- `getBoundingRect()` returns the element's absolute drawn rect (a
  `layout::Rect`, scroll- and clamp-correct) — the analog of the DOM's
  `getBoundingClientRect()`. Feed it into the placement helpers (see below) to
  position floating panels.
- `.ref(...)` only exists on host elements (Box, Text, Input, …), not on
  `Component` — a component has no single node, so a ref on one is a compile
  error, not a silent no-op.

For positioning context menus / dropdowns / cascading submenus from a measured
rect, see [Integration Notes — floating-panel placement](integration-notes.md)
and `yui/layout/Placement.hpp` (`placePanel`, `placeSubmenu`).

### Hook Rules

Hooks must be called in the same order every render — no conditional hooks:

```cpp
// WRONG - hooks in conditional
auto comp = [](ComponentContext& ctx) -> VNode {
    if (someCondition) {
        auto [x, setX] = ctx.useState(0);  // Bad: not called every render
    }
    // ...
};

// RIGHT - always call hooks, use values conditionally
auto comp = [](ComponentContext& ctx) -> VNode {
    auto [x, setX] = ctx.useState(0);
    if (someCondition) {
        // Use x here
    }
    // ...
};
```

## Store Subscriptions

`Store<T>::use()` inside a component subscribes only that component. When the Store changes, only subscribed components re-render — siblings and parents are untouched:

```cpp
Store<int> counter(0);

auto Display = [&](ComponentContext& ctx) -> VNode {
    int n = counter.use();  // Subscribes this component
    return Text(std::to_string(n));
};

auto Static = [](ComponentContext& ctx) -> VNode {
    return Text("I never re-render");  // No subscription
};

// When counter.set(42) is called:
//   Display re-renders (subscribed)
//   Static does NOT re-render
Column({
    Component(Display),
    Component(Static),
})
```

## Conditional Rendering

Built-in helpers for conditional content:

```cpp
// When - show content only if condition is true
VNode When(bool condition, VNode node);

// If - choose between two alternatives
VNode If(bool condition, VNode ifTrue, VNode ifFalse);

// Usage
Column({
    Text("Settings"),
    When(showAdvanced, AdvancedPanel()),
    When(isAdmin, AdminPanel()),
})
```

**Note:** `When(false, ...)` returns `VNode::empty()` which occupies a position slot but doesn't render. This ensures siblings maintain correct position matching when the condition toggles.

## Keyed Lists

Two built-in list helpers that automatically assign keys. Render functions can return either `VNode` or `Component`:

```cpp
// Vertical list (Column direction)
template<typename T, typename KeyFn, typename RenderFn>
VNode List(const std::vector<T>& items, KeyFn keyFn, RenderFn renderFn);

// Horizontal list (Row direction)
template<typename T, typename KeyFn, typename RenderFn>
VNode HList(const std::vector<T>& items, KeyFn keyFn, RenderFn renderFn);

// Usage with VNode render function
VNode UserList(const std::vector<User>& users) {
    return List(users,
        [](const User& u) { return u.id; },  // Key (string or number)
        [](const User& u) {
            return Row({
                Text(u.name).flexGrow(1),
                RoleBadge(u.role),
            }).height(24).padding(4);
        }
    );
}

// Usage with Component render function (each item gets its own state)
VNode TodoList(const std::vector<Todo>& todos) {
    return List(todos,
        [](const Todo& t) { return t.id; },
        [](const Todo& t) -> Component {
            return [t](ComponentContext& ctx) -> VNode {
                auto [editing, setEditing] = ctx.useState(false);
                return Row({
                    Text(t.text).flexGrow(1),
                    Box(Text("Edit")).onClick([=] { setEditing(!editing); }),
                });
            };
        }
    );
}
```

Keys can be strings or any type convertible via `std::to_string()`.

## Layout Helpers

```cpp
// Row - horizontal flex container
VNode Row(std::vector<Child> children);

// Column - vertical flex container
VNode Column(std::vector<Child> children);

// Spacer - flexible space that grows to fill
VNode Spacer();

// Gap - fixed-size spacing
VNode Gap(float size);
```

`Child` is `std::variant<VNode, Component>`, so Row/Column accept any mix.

## Component Keys

Components support keys for stable reconciliation in lists:

```cpp
Component(MyComp).setKey("unique-id")
Component(MyComp).setKey(42)  // Integer keys (more efficient)
```

## Composition Example

```cpp
Component LoginForm() {
    return [](ComponentContext& ctx) -> VNode {
        auto [username, setUsername] = ctx.useState<std::string>("");
        auto [password, setPassword] = ctx.useState<std::string>("");

        return Column({
            Text("Login").fontSize(16),
            LabeledField("Username",
                Input().value(username)
                    .onChange([=](const std::string& v) { setUsername(v); })),
            LabeledField("Password",
                Input().value(password).password(true)
                    .onChange([=](const std::string& v) { setPassword(v); })),
            Row({
                Button("Cancel", [] { /* ... */ }),
                Button("Submit", [=] { doLogin(username, password); }),
            }).gap(8),
        }).gap(16).padding(20);
    };
}
```
