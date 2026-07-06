# Widgets

`yui::widgets` is a standard set of controlled UI controls built on the core
primitives: `Button`, `Checkbox`, `Switch`, `Radio`/`RadioGroup`, `Progress`,
`Slider`, `Tabs`, `Select`. They are header-only — `<yui/yui.hpp>` pulls in all
of them, no separate include needed.

## Shared conventions

**Controlled.** Every widget takes its current value as a constructor argument
and reports changes via an `onChange`/`onSelect` callback — the widget never
owns its own value. The app holds the state and re-renders with the new value,
the same pattern as `Input`.

**Keyboard nav is opt-in.** Core stays keycode-agnostic (SDL and GLFW disagree
on key codes), so a widget attaches no `onKeyDown` at all until you hand it a
keycode via a `*KeyCode` setter. Once set, that handler runs to completion for
**every** key while the widget is focused — it consumes the whole keyboard,
not just the codes you named. Leave the keycode(s) unset to keep the widget
silent on the keyboard entirely. This contract is identical across `Checkbox`,
`Switch`, `Tabs`, `Slider`, and `Select`.

**Conversion seam.** Some widgets are pure builders (convert to both `VNode`
and `Child`); others are stateful `Component`s under the hood and convert only
to `Child` (there's no single `VNode` to hand back). Trying to bind a
`Child`-only widget to a bare `VNode` is a compile error, not a silent no-op.

| Widget | Kind | Converts to |
|---|---|---|
| `Button` | Component (transient `pressed` state) | `Child` only |
| `Checkbox` | pure builder | `VNode` and `Child` |
| `Switch` | pure builder | `VNode` and `Child` |
| `Radio` / `RadioGroup` | pure builder | `VNode` and `Child` |
| `Progress` | pure builder | `VNode` and `Child` |
| `Slider` | Component (drag state) | `Child` only |
| `Tabs` | pure builder | `VNode` and `Child` |
| `Select` | Component (open/highlight state) | `Child` only |

## Button

```cpp
ButtonBuilder Button(std::string label);   // themed Text label
ButtonBuilder Button(std::vector<Child> content);  // arbitrary content
ButtonBuilder Button(Cs&&... content);     // variadic content, same as above
```

```cpp
Button("Save")
    .onClick([&] { save(); })
    .disabled(!canSave)
    .backgroundColor(0x3A3A3AFF).hoverColor(0x4A4A4AFF)
    .pressedColor(0x2A2A2AFF).disabledColor(0x2A2A2AFF)
    .textColor(0xFFFFFFFF).disabledTextColor(0x808080FF)
    .fontSize(14).borderRadius(4).padding(8);
```

Two content paths: `Button("label")` builds a themed `Text` child (`textColor`
/`disabledTextColor`/`fontSize` apply to it); `Button(children...)` wraps
arbitrary content, and the label-only setters become inert.

`onClick` is always attached — even when `disabled(true)`, in which case it's a
consuming no-op, so a disabled click never bubbles into a parent's `onClick`.
Hover fill is suppressed while disabled. The "pressed" fill is internal
`useState` — there's no `pressedStyle` primitive in core, since press is
transient UI state, unlike hover/focus.

## Checkbox

```cpp
CheckboxBuilder Checkbox(bool checked);
```

```cpp
Checkbox(settings.darkMode)
    .onChange([&](bool v) { setDarkMode(v); })
    .label("Dark mode")
    .disabled(false)
    .boxColor(0x808080FF).checkColor(0x4A90D9FF)
    .size(16)
    .textColor(0xE0E0E0FF).fontSize(13)
    .toggleKeyCode(SDLK_SPACE);  // opt-in; unset = no key handler
```

Pure builder, no transient state — the inner check mark always mirrors the
`checked` value passed in this render. The mark is a plain filled+rounded
inner box (no glyph, so no font dependency). Clicking (even while disabled)
fires a consuming click so it never bubbles.

## Switch

```cpp
SwitchBuilder Switch(bool on);
```

```cpp
Switch(audio.muted)
    .onChange([&](bool v) { setMuted(v); })
    .label("Mute")
    .trackOffColor(0x808080FF).trackOnColor(0x4A90D9FF).chipColor(0xFFFFFFFF)
    .size(36, 20)   // two-arg: track width, height
    .toggleKeyCode(SDLK_SPACE);
```

Same controlled/stateless contract as `Checkbox`, drawn as a sliding pill —
the chip position and track color are entirely derived from `on`. `.size(w,
h)` resizes the whole control; the chip is inset by a fixed padding and its
travel distance is `width - height`.

## Radio / RadioGroup

```cpp
RadioBuilder Radio(bool selected);
RadioGroupBuilder RadioGroup(std::vector<std::string> options);
```

Usually you want the group, not a lone `Radio`:

```cpp
RadioGroup({"Small", "Medium", "Large"})
    .value(sizeIndex)
    .onChange([&](int i) { setSizeIndex(i); })  // fires only when i changes
    .direction(FlexDirection::Column)  // default; Row also works
    .gap(6)
    .dotColor(0x4A90D9FF).ringColor(0x808080FF)
    .size(16)
    .disabled(false);
```

`RadioGroup` builds one keyed `Radio` per option internally and reports the
chosen index. Selecting the already-selected index is a no-op — `onChange`
only fires on an actual change, mirroring `Select`'s dedup contract.

A standalone `Radio` is the same circle-with-dot control, taking `onSelect`
(no argument — fires unconditionally on click) instead of `onChange`, plus
`.setKey(...)` for placing it manually in a list you build yourself.

## Progress

```cpp
ProgressBuilder Progress(float value);  // 0..1, clamped
```

```cpp
Progress(downloadFraction)
    .trackColor(0x2A2A2AFF).fillColor(0x4A90D9FF)
    .height(8)
    .borderRadius(4);  // unset -> full pill (height / 2)
```

The one non-interactive widget: purely visual, no events, no consuming click
handler — clicks pass straight through to whatever is behind/around it. The
fill's width *is* the value (no text glyph involved), which also makes it
trivial to assert against in tests.

## Slider

```cpp
SliderBuilder Slider(float value);  // 0..1, clamped
```

```cpp
Slider(volume)
    .onChange([&](float v) { setVolume(v); })  // press-jump + every changing drag move
    .step(0.05f)      // quantize; unset = continuous
    .disabled(false)
    .trackColor(0x2A2A2AFF).fillColor(0x4A90D9FF).thumbColor(0xFFFFFFFF)
    .trackHeight(4).thumbSize(14)
    .decrementKeyCode(SDLK_LEFT).incrementKeyCode(SDLK_RIGHT).keyStep(0.05f);
```

A stateful `Component` (unlike Checkbox/Switch/Radio): it needs an element ref
to map pointer pixels back onto a 0..1 value, and it uses the pointer-capture +
drag model directly — see [Events and Interaction — pointer capture and
drag](primitives.md#pointer-capture-and-drag). The wrapper carries all three
mouse handlers, making it the press target and therefore the implicit captor,
so a drag past the track edge clamps cleanly to 0/1 instead of losing the
pointer. The internal "dragging" flag is a `useRef`, not `useState` —
flipping it must not itself trigger a re-render; the visual update comes from
the app's `onChange`-driven re-render.

## Tabs

```cpp
TabsBuilder Tabs();  // starts empty; build with .tab(...)
```

```cpp
Tabs()
    .tab("General", GeneralPanel())
    .tab("Advanced", AdvancedPanel())
    .active(activeTab)
    .onChange([&](int i) { setActiveTab(i); })
    .prevKeyCode(SDLK_LEFT).nextKeyCode(SDLK_RIGHT)  // opt-in arrow cycling, wraps
    .stripColor(0x1E1E1EFF).tabColor(0x2A2A2AFF).activeTabColor(0x3A3A3AFF)
    .hoverTabColor(0x333333FF)
    .textColor(0x808080FF).activeTextColor(0xFFFFFFFF)
    .fontSize(14).tabPadding(8);
```

A pure builder (no transient state) — `active` is fully controlled. **Only
the active panel is instantiated**, keyed `"panel-<active>"`, so switching
tabs **unmounts** the previous panel and **mounts** the new one; it does not
reconcile one panel's tree into another. A panel's local `useState`/`useRef`
is lost on switch and does not come back when you switch back. If a panel
needs state to survive tab switches, lift that state into a `Store` above the
`Tabs` rather than inside the panel component.

## Select

```cpp
SelectBuilder Select(std::vector<std::string> options);
```

```cpp
Select({"Small", "Medium", "Large"})
    .value(sizeIndex)               // -1 = show the placeholder
    .onChange([&](int i) { setSizeIndex(i); })
    .placeholder("Choose a size...")
    .disabled(false)
    .maxListHeight(240)             // popup height cap; excess scrolls
    .upKeyCode(SDLK_UP).downKeyCode(SDLK_DOWN)
    .selectKeyCode(SDLK_RETURN).dismissKeyCode(SDLK_ESCAPE)
    .controlColor(0x2A2A2AFF).hoverColor(0x333333FF)
    .listColor(0x1E1E1EFF).optionHoverColor(0x333333FF)
    .highlightColor(0x4A90D9FF).selectedColor(0x37373DFF)
    .textColor(0xE0E0E0FF).fontSize(14)
    .borderRadius(4).borderColor(0x808080FF).borderWidth(1);
```

A stateful `Component` (open/closed, placement, and the keyboard-highlighted
row are internal; the selected *value* stays controlled). It's built from
[`Portal`](overlays.md#portal) — the popup renders at root z-order over a
transparent, full-viewport backdrop whose click closes it (the `Modal`
outside-click idiom), with its position computed via `layout::placePanel`
against the control's drawn rect (the exact mechanism `Tooltip.show()` uses;
see [Overlays](overlays.md)).

Keyboard nav is opt-in and, notably, **only attached while the popup is
open** — a closed `Select` has no key handler at all, so it can't be opened
from the keyboard (open by click only) and never black-holes app hotkeys
while closed. `Select` also deliberately does **not** close on blur: clicking
an option row blurs the control before the row's own `onClick` commits, so a
blur-to-close handler would race the selection and dismiss the popup before
it lands. Click-away is handled by the backdrop instead, and Esc by
`dismissKeyCode`.

Re-selecting the already-chosen index still closes the popup but does not
fire `onChange` (same dedup contract as `RadioGroup`).
