# Overlays

Floating UI — dropdowns, dialogs, tooltips, context menus — needs to escape
its parent's clipping and layout, and sit above everything else for hit
testing. `Portal` is the one core primitive that provides this; `Modal` and
`Tooltip` (in `yui::widgets`) are built entirely on top of it, with no special
core support of their own.

## Portal

```cpp
VNode Portal(std::vector<Child> children);
VNode Portal(Cs&&... children);   // variadic, same as Box
```

```cpp
Portal(backdrop)
    .trapFocus();   // scope Tab to the portal's content; save/restore focus
```

A `Portal`'s content **reconciles in its logical parent** — where it's
declared in the tree — so component state, hooks, and element refs behave
exactly as if it weren't in a portal at all. But it is **laid out against the
viewport** rather than its parent's box, and **painted and hit-tested at root
z-order**, escaping any ancestor's clip and sitting above the rest of the
tree. Position its content with `.positionType(PositionType::Absolute)` plus
`.positionLeft/positionTop(...)` in root-space (viewport) coordinates — the
same coordinates [`NodeRef::getBoundingRect()`](components.md#useelementref)
and the [placement helpers](integration-notes.md) already work in.

The `Portal` node itself occupies zero space in its parent's layout (no gap
slot, no size) — it's pure plumbing. It has no visual setters; all visuals
belong to its content.

`.trapFocus(bool = true)` scopes Tab traversal to the portal's content while
it's mounted. Mounting saves whatever was focused beforehand; unmounting (or
clearing the trap) restores it. This is exactly what `Modal` uses to give a
dialog focus-trap behavior with zero extra wiring — see
[Focus](primitives.md#focus).

When multiple portals are open at once (e.g. a `Select` popup opened from
inside a `Modal`), they layer in **document order**: a portal declared later
in a pre-order scan of the main tree paints above one declared earlier, and a
portal nested inside another portal's content layers above its spawner. Paint
order and hit-test order are guaranteed to agree.

## Modal

```cpp
ModalBuilder Modal(std::vector<Child> children);
ModalBuilder Modal(Cs&&... children);   // variadic
```

```cpp
When(showDialog, Modal(
    Text("Delete this item?"),
    Row(
        Button("Cancel", [&] { setShowDialog(false); }),
        Button("Delete", [&] { deleteItem(); setShowDialog(false); })
    ).gap(8)
)
    .onDismiss([&] { setShowDialog(false); })
    .dismissOnBackdropClick(true)      // default true
    .dismissKeyCode(SDLK_ESCAPE)       // unset = no key dismiss
    .backdropColor(0x00000088u));      // default: half-transparent black
```

`Modal(...)` is `Portal(backdrop-wrapped-panel).trapFocus()` — nothing more.
Open/close is entirely app state: render the `Modal` while it should be open,
stop rendering it to close. Mounting arms the focus trap (and saves the
previously-focused node); unmounting restores that focus automatically.

The scrim is a full-viewport backdrop; your children are wrapped in a panel
that's a *child* of the backdrop (so the backdrop centers it) with a
consuming `onClick` — a click inside the dialog bubbles toward the backdrop
and is swallowed there, while a click beside the dialog hits the backdrop
directly and calls `onDismiss` (when `dismissOnBackdropClick` is true; when
false, the backdrop still consumes the click, so the modal stays modal but
the click does nothing).

`.dismissKeyCode(...)` is app-supplied for the same reason every widget
keycode is: core doesn't know whether you're running under SDL or GLFW. The
widget never closes itself — `onDismiss` is a request, not a command; your app
decides what "closed" means (usually flipping a bool).

## Tooltip

```cpp
TooltipBuilder Tooltip(std::vector<Child> target);
TooltipBuilder Tooltip(Cs&&... target);   // variadic
```

```cpp
Tooltip(
    Box(Text("?")).padding(4)
)
    .tip("Enables verbose logging")   // convenience: default-styled one-line text
    .delayMs(400)                     // unset = render_defaults::kHoverDelayMs
    .tipSize(160, 24);                // helps placement clamp fully on-screen
```

Or supply arbitrary tip content instead of a string:

```cpp
Tooltip(icon).tip(Column(Text("Shortcut"), Text("Ctrl+K")).gap(2));
```

Unlike `Modal`, `Tooltip` is a stateful `Component` — visibility and position
are internal state, set by an `onHoverDelay` callback and cleared on hover
leave — so it converts to `Child` only, not `VNode`.

The tip's `Portal` is declared *inside* the target wrapper deliberately:
portal content hover-walks through its logical parent, so the wrapper is the
lowest common ancestor of any pointer travel between the target and the tip.
That means moving from the target onto the tip itself doesn't fire
`onHover(false)` mid-travel and flicker the tip away — only leaving both dismisses
it. Placement is computed in the hover-delay callback (not during render,
since element refs read null then — see [`useElementRef`](components.md#useelementref)) by
reading the wrapper's drawn rect and calling the same `layout::placePanel`
helper `Select`'s popup uses, clamped against the render root as the
viewport. The tip sits flush against the target's bottom edge (zero gap) —
a nonzero gap would open a dead zone the pointer could pass through mid-drag,
causing the tip to unmount before it's reached.

See [Integration Notes — floating-panel placement](integration-notes.md) for
`placePanel`/`placeSubmenu` directly, if you're building a custom overlay
that isn't quite `Modal` or `Tooltip` (e.g. a cascading context menu).
