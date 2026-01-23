# Components

Components are functions returning VNodes. No registration, no base class.

## Basic Pattern

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

Two built-in list helpers that automatically assign keys:

```cpp
// Vertical list (Column direction)
template<typename T, typename KeyFn, typename RenderFn>
VNode List(const std::vector<T>& items, KeyFn keyFn, RenderFn renderFn);

// Horizontal list (Row direction)
template<typename T, typename KeyFn, typename RenderFn>
VNode HList(const std::vector<T>& items, KeyFn keyFn, RenderFn renderFn);

// Usage
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
```

Keys can be strings or any type convertible via `std::to_string()`.

## Layout Helpers

```cpp
// Row - horizontal flex container
VNode Row(std::vector<VNode> children);

// Column - vertical flex container
VNode Column(std::vector<VNode> children);

// Spacer - flexible space that grows to fill
VNode Spacer();

// Gap - fixed-size spacing
VNode Gap(float size);
```

## Composition Example

```cpp
VNode LoginForm(LoginState& state) {
    return Column({
        Text("Login").fontSize(16),
        LabeledField("Username", Input(&state.username)),
        LabeledField("Password", Input(&state.password).password(true)),
        Row({
            Button("Cancel", state.onCancel),
            Button("Submit", state.onSubmit),
        }).gap(8),
    }).gap(16).padding(20);
}
```
