# Primitives

## Box

Layout container. The fundamental building block.

```cpp
VNode Box(std::vector<Child> children);  // Mixed VNode/Component children, dynamic path
VNode Box(Cs&&... children);            // Variadic: each argument its own Child slot (prefer this)
VNode Box();                            // Empty (spacers, dividers)

Box(...)
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

**Prefer variadic arguments over a brace-enclosed vector**: `Box(a, b, c)` forwards each
argument straight into a `Child` slot, while `Box({a, b, c})` builds a
`std::vector<Child>` first and is measurably slower (a deep-copy penalty on
construction). Both compile and behave identically — the brace-list form
still exists for the dynamic case (a runtime-built vector, e.g. from a loop)
— but for a fixed, known set of children, write `Box(a, b, c)`, not
`Box({a, b, c})`. This applies to every primitive/helper factory that takes
children (`Box`, `Scroll`, `Portal`, `Row`, `Column`, `Modal`, `Tooltip`).

Factories are marked `[[nodiscard]]`: discarding the result of a bare
`Box().width(100);` statement is always a mistake, and the compiler will
warn.

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
    .font("mono")           // registered font face by name; unset = default
    .backgroundColor(0x333333FF)
    .borderColor(0x666666FF)
    .borderRadius(4)
    .color(0xFFFFFFFF)
    .fontSize(12)
    .onChange([=](const std::string& v) { setValue(v); })
    .onSubmit([=] { submit(); });
```

`.multiline()` turns an `Input` into a textarea (soft-wraps, Enter inserts a
newline, grows to fit its lines) — `.password()` wins over `.multiline()` if
both are set. Full caret/selection editing, clipboard, and the
`EditCommand` routing model are covered in [Text Editing](text-editing.md).

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

### Scrolling programmatically

```cpp
auto scrollRef = ctx.useElementRef();

Scroll(items).ref(scrollRef);

// later, in a handler:
if (auto* scroll = scrollRef.asScroll()) {
    scroll->scrollTo(0, 400);                       // clamped, smooth-interpolated
    scroll->scrollIntoView(someRowRef.getBoundingRect());  // bring a rect into the padded viewport
}
```

`NodeRef::asScroll()` is the sanctioned, type-checked route to the scroll
API — it returns the live `ScrollNode*`, or `nullptr` if the ref is
unattached, dead, or attached to a non-`Scroll` element. `scrollTo(x, y)`
sets a target offset (clamped like every other scroll write) that the
existing smooth interpolation animates toward. `scrollIntoView(rect)` takes
an **absolute-space** rect — typically from
[`NodeRef::getBoundingRect()`](components.md#useelementref) on some row
inside the scroll — and scrolls the minimum needed to bring it into the
padded viewport; a rect already fully visible changes nothing, and a rect
larger than the viewport aligns its near edge.

`Scroll` also draws its own overlay scrollbars when content overflows.
Presses on a scrollbar's thumb or track are consumed as chrome — they never
reach your `onClick`/`onMouseDown` handlers, never change focus, and never
chain into a click.

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

## Portal

Detached-content container — the primitive floating UI (dropdowns, dialogs,
tooltips) is built on. See [Overlays](overlays.md#portal) for the full
writeup; summary:

```cpp
VNode Portal(std::vector<Child> children);
VNode Portal(Cs&&... children);   // variadic

Portal(popupContent)
    .trapFocus();  // scope Tab inside; save/restore focus across mount/unmount
```

Content reconciles in its logical parent (state/hooks/refs behave normally)
but is laid out against the viewport and painted/hit-tested at root z-order,
escaping any ancestor clip. Position content with
`.positionType(PositionType::Absolute)` + `.positionLeft/positionTop(...)` in
root-space coordinates. The `Portal` node itself has no visual setters and
occupies zero space in its parent's layout.

## Events

All primitives support event handlers:

```cpp
Box(...)
    .onClick([&] { selectItem(); })
    .onDoubleClick([&] { openItem(); })
    .onRightClick([&] { showContextMenu(); })
    .onMiddleClick([&] { openInBackground(); })
    .onHover([](bool hovered) { highlight(hovered); })
    .onHoverDelay([&] { showTooltip(); })   // fires once after hoverDelayMs of continuous hover
    .onFocus([](bool focused) { ... })
    .onScroll([](float dx, float dy) { handleScroll(dx, dy); })
    .cursor(CursorShape::Pointer);

Text("Delete")
    .color(RED)
    .onClick(onDelete);
```

`onClick` requires the press **and** the release to land on the same node —
a press that starts on one element and is released over another (e.g. the
pointer dragged off before releasing) does not fire `onClick` on either.

The `onScroll` handler receives scroll delta values. For ScrollNode, scroll is handled automatically — use `onScroll` on other elements for custom scroll behavior.

### Pointer capture and drag

```cpp
Box(...)
    .onMouseDown([](float x, float y, MouseButton btn, uint16_t mods) { ... })
    .onMouseUp([](float x, float y, MouseButton btn) { ... })
    .onMouseMove([](float x, float y) { ... })
    .onDrag([](const DragEvent& e) {
        // e.x, e.y (current), e.dx, e.dy (delta since last captured move),
        // e.startX, e.startY (press anchor), e.button
    });
```

The node a press lands on becomes the **implicit captor** for that gesture:
while the button stays held, `onMouseMove` routes to it even if the pointer
leaves the node (or the window), and `onMouseUp` is delivered to it wherever
the release actually happens. This is what lets a slider thumb or a drag
handle track the pointer past its own bounds without losing the gesture.
`onDrag` fires once the pointer has moved past a small threshold
(`render_defaults::kDragThresholdPx`, 4px) from the press anchor — it's the
"this became a drag, not a click" signal, separate from the raw
`onMouseMove` stream. [`Slider`](widgets.md#slider) is a worked example of
building a drag-driven control on top of these three handlers.

`.cursor(CursorShape)` requests a pointer shape while hovered or captured —
`Arrow`, `IBeam`, `Pointer`, `Crosshair`, `ResizeEW`, `ResizeNS`,
`ResizeAll`. `Host::getCursor()` resolves the cursor to show right now (the
captor's cursor during a drag, else the hovered chain's; an `Input`
defaults to `IBeam` with nothing set, everything else to `Arrow`) — poll it
once per frame after `update()` and map it to your platform's native cursor.

### Focus

```cpp
Box(...)
    .focusable(true)        // click/Tab can move focus here (an Input always can)
    .autoFocus(true)         // focus this node when it mounts
    .onFocus([](bool focused) { ... });
```

`Host` owns the focus API: `host.focus(node)` / `host.blur()` for
programmatic focus (accepts any node — `.focusable()` gates click/Tab
acquisition, not programmatic focus, so `autoFocus` and `host.focus(...)`
work regardless), `host.focusNext()`/`focusPrev()` for document-order Tab
traversal (wrapping, scoped to an active focus trap), and
`host.setFocusTrap(node)`/`clearFocusTrap()` to scope Tab to a subtree —
what [`Modal`](overlays.md#modal) uses via `Portal::trapFocus()`. Wire your
platform's Tab key into traversal through the `focusNav` flag on the
5-argument `Host::handleKeyDown` overload — see
[Text Editing](text-editing.md#caret-selection-and-editcommand), since it
shares the same routing call. Focus navigation is unswallowable: it runs
ahead of app `onKeyDown` handlers.

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
VNode Row(std::vector<Child> children);    // Box with FlexDirection::Row (dynamic path)
VNode Row(Cs&&... children);               // variadic — prefer this for a fixed set
VNode Column(std::vector<Child> children); // Box with FlexDirection::Column
VNode Column(Cs&&... children);            // variadic
VNode Spacer();                            // Box with flexGrow(1)
VNode Gap(float size);                     // Box with fixed width/height
```

`Child` is `std::variant<VNode, Component>`, so Row/Column accept any mix of VNodes and Components. See the [variadic-vs-brace-list note](#box) under Box — the same tradeoff applies here.
