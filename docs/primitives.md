# Primitives

## Box

Layout container. The fundamental building block.

```cpp
VNode Box(std::vector<Child> children);  // Mixed VNode/Component children
VNode Box(VNode child);                  // Single VNode child
VNode Box(Component child);             // Single Component child
VNode Box();                            // Empty (spacers, dividers)

Box({...})
    .width(100).height(50)
    .minWidth(50).maxWidth(200)
    .flexDirection(FlexDirection::Row)
    .justifyContent(JustifyContent::Center)
    .alignItems(AlignItems::Stretch)
    .alignSelf(AlignSelf::Center)
    .flexGrow(1).flexShrink(0).flexBasis(100)
    .padding(8).paddingTop(4).paddingLeft(8)
    .margin(4).marginTop(2)
    .gap(4)
    .backgroundColor(0x202020FF)
    .borderColor(0x404040FF)
    .borderWidth(1)
    .borderRadius(4)
    .setKey("my-box");
```

`Child` is `std::variant<VNode, Component>` — children can be primitives, helper function results, or stateful components.

## Text

Displays text. Intrinsic size based on content (uses Yoga measure function).

```cpp
VNode Text(std::string content);

Text("Hello")
    .fontSize(12)
    .color(0xFFFFFFFF);
```

## Input

Text input field. Uses a controlled pattern — set the value and handle changes via callbacks:

```cpp
VNode Input();

Input()
    .value(currentText)
    .placeholder("you@example.com")
    .password(true)
    .backgroundColor(0x333333FF)
    .borderColor(0x666666FF)
    .borderRadius(4)
    .color(0xFFFFFFFF)
    .fontSize(12)
    .onChange([=](const std::string& v) { setValue(v); })
    .onSubmit([=] { submit(); });
```

Typically used with `useState` or `useField` inside a stateful component:

```cpp
auto FormComp = [&](ComponentContext& ctx) -> VNode {
    auto [email, setEmail] = ctx.useState<std::string>("");

    return Input()
        .value(email)
        .placeholder("you@example.com")
        .onChange([=](const std::string& v) { setEmail(v); });
};
```

## Scroll

Scrollable container with clipping. Content that exceeds bounds is clipped and can be scrolled.

```cpp
VNode Scroll(VNode child);
VNode Scroll(Component child);
VNode Scroll(std::vector<Child> children);

Scroll(
    Column({
        // Long list of items that exceeds container height
        ...items
    })
)
    .width(200).height(300)
    .backgroundColor(0x202020FF);
```

ScrollNode automatically handles scroll events when content exceeds container bounds. Scroll offset is managed internally with smooth interpolation, and children are rendered with clipping.

**Layout note**: ScrollNode children are laid out with unconstrained height, allowing content to exceed the container's bounds. This is different from normal Yoga layout where children are constrained to fit.

Supports the same visual properties as Box: `backgroundColor`, `borderColor`, `borderWidth`, `borderRadius`, and hover/focus styles.

## Canvas

Custom drawing surface. The draw function receives a renderer-specific context:

```cpp
VNode Canvas(CanvasDrawFn draw);

Canvas([](void* ctx, float w, float h) {
    auto* vg = static_cast<NVGcontext*>(ctx);
    // Draw with NanoVG API...
}).width(200).height(200);
```

The draw function receives:
- `ctx` — Renderer-specific context (NVGcontext* for NanoVG, SDL_Renderer* for SDL)
- `w`, `h` — Canvas dimensions after layout

Draw relative to (0,0) — the renderer handles positioning.

## Events

All primitives support event handlers:

```cpp
Box({...})
    .onClick([&] { selectItem(); })
    .onRightClick([&] { showContextMenu(); })
    .onHover([](bool hovered) { highlight(hovered); })
    .onFocus([](bool focused) { ... })
    .onScroll([](float dx, float dy) { handleScroll(dx, dy); });

Text("Delete")
    .color(RED)
    .onClick(onDelete);
```

The `onScroll` handler receives scroll delta values. For ScrollNode, scroll is handled automatically — use `onScroll` on other elements for custom scroll behavior.

## State-Based Styles

Override visual properties on hover/focus without manual state tracking:

```cpp
// Box
Box({...})
    .backgroundColor(0x333333FF)
    .hoverStyle(BoxStyle{.backgroundColor = 0x444444FF})
    .focusStyle(BoxStyle{.borderColor = 0x4a9fffFF});

// Text
Text("Click me")
    .color(0xFFFFFFFF)
    .hoverStyle(TextStyle{.color = 0x88CCFFFF});

// Input
Input()
    .value(text)
    .borderColor(0x666666FF)
    .hoverStyle(InputStyle{.borderColor = 0x888888FF})
    .focusStyle(InputStyle{.borderColor = 0x4a9fffFF});
```

Style structs use `std::optional` - only set fields you want to override:

```cpp
struct BoxStyle {
    std::optional<uint32_t> backgroundColor;
    std::optional<uint32_t> borderColor;
    std::optional<float> borderWidth;
    std::optional<float> borderRadius;
};

struct TextStyle {
    std::optional<uint32_t> color;
    std::optional<float> fontSize;
};

struct InputStyle {
    std::optional<uint32_t> backgroundColor;
    std::optional<uint32_t> borderColor;
    std::optional<float> borderWidth;
    std::optional<float> borderRadius;
    std::optional<uint32_t> color;
    std::optional<float> fontSize;
};
```

Focus takes precedence over hover. Layout props (width, padding, etc.) are not in style structs — they'd cause expensive relayout.

## Layout Enums

```cpp
enum class FlexDirection { Row, Column, RowReverse, ColumnReverse };
enum class JustifyContent { FlexStart, Center, FlexEnd, SpaceBetween, SpaceAround, SpaceEvenly };
enum class AlignItems { FlexStart, Center, FlexEnd, Stretch, Baseline };
enum class AlignSelf { Auto, FlexStart, Center, FlexEnd, Stretch, Baseline };
```

## Helpers

Convenience functions composing primitives:

```cpp
VNode Row(std::vector<Child> children);    // Box with FlexDirection::Row
VNode Column(std::vector<Child> children); // Box with FlexDirection::Column
VNode Spacer();                            // Box with flexGrow(1)
VNode Gap(float size);                     // Box with fixed width/height
```

`Child` is `std::variant<VNode, Component>`, so Row/Column accept any mix of VNodes and Components.
