# Integration Notes

Sharp edges found while building real applications on yui — and the sanctioned
patterns for them. These are the things that bit a real integration (a cascading
module browser with scrollable, nested menus), framed so the next consumer
doesn't re-derive and re-hit the same bugs.

The core model is sound: once state lives in a `Store`, the dirty/reconcile
machinery delivers live updates with no flicker, and the cross-thread `set()`
contract is clean. Every item below is about the *edges* — discoverability and
missing utilities — not the core.

---

## The reactivity boundary

**yui only re-renders for state it can see.** That means state held in a `Store`
or a `useState` slot. Anything else — a plain `std::set` on an object you own, a
field on a third-party struct, a global — is **invisible** to yui. Mutating it
from an event handler changes your data but flips no dirty flag, so **nothing
re-renders** and there is no warning.

```cpp
// SILENT NO-OP: handler mutates external state yui can't see.
Box(...).onClick([state] { state->hiddenModules.insert(id); });
//                         ^ data changes, UI does not. No error.
```

This is the single most common trap for a React developer arriving at yui,
because C++ hands you a mutable object next to the component that React never
would. There is no diagnostic for it — yui has no knowledge the external object
exists, so it cannot notice a setter *wasn't* called. The boundary rule is the
defense: **if a change must update the UI, the changed state must live in a
`Store` (shared/long-lived) or `useState` (component-local).**

The fix for the example above is to hold the canonical state in a `Store` that
lives on the long-lived object, mutate via `set()`, and `use()` it in render:

```cpp
// store lives on the long-lived object (survives menu open/close cycles)
Box(...).onClick([store] { store->set([&](auto& s) { s.hidden.insert(id); }); });
// ...and somewhere in the render body:  const auto& s = store->use();
```

See [components.md](components.md) for the `Store`-vs-`useState` lifetime rule:
`useState` survives re-renders but dies on unmount; `Store` lives outside the
component tree and spans many mount/unmount cycles. State that must outlive a
single open of a menu/panel belongs in a `Store`.

---

## Reading Store state in deferred closures: `use()` to subscribe, `peek()` to read

`Store` has two readers, and they are not interchangeable:

- **`use()`** returns `T` **by value** (a copy) **and subscribes** the current
  fiber. Its job is subscription, and it is only valid *during the render that
  calls it*.
- **`peek()`** returns `T` **by value without subscribing** — a live read of the
  current value, valid any time on the host thread.

Both return by value (a copy taken under the store lock), so there is no
reference to dangle. The remaining footgun is *staleness*: a builder lambda or
callback that runs **after** the render — a deferred row builder, an async
completion, a cascade panel built lazily — must not capture the `use()`'d value.
Capturing the returned copy freezes a stale snapshot taken at lambda-creation
time, so the deferred code renders old state.

```cpp
// WRONG: captures a frozen snapshot; deferred build shows stale state.
auto s = store->use();
auto buildRow = [s] { return Row(renderFrom(s)); };   // s is stale when this runs

// RIGHT: subscribe once in render, read live at call time.
store->use();                                          // subscribe THIS fiber
auto buildRow = [store] { return Row(renderFrom(store->peek())); };
```

The rule: **subscribe with `use()` in the render body; read with `peek()` inside
any closure that runs after render.** You still need the `use()` somewhere in the
render — without it the deferred `peek()` reads live but nothing ever triggers a
re-render. Both calls are required; they do different jobs.

---

## Transient held-modifier state (e.g. "reveal while Alt is held")

There is no per-frame / per-tick hook in yui, by design — `useEffect` runs after
render, not every frame, and a per-frame re-render is exactly the immediate-mode
behavior the retained model exists to avoid. So continuous input ("show hidden
items while Alt is held") is **edge-triggered into a `Store` flag**, not polled:

```cpp
// on key down/up events (not per frame):
onKeyDown: store->set([](auto& s){ s.revealHeld = true; });
onKeyUp:   store->set([](auto& s){ s.revealHeld = false; });
```

This works and is the right retained-mode answer — but it has an asymmetry the
basic idiom misses: **if the component unmounts while the key is held, the
key-up never arrives and the flag sticks.** Close a menu with Alt down, reopen
it, and the reveal is still on.

The complete pattern resets the flag once when the component mounts, so a stuck
flag from a previous lifetime can't leak in.

Note a yui difference from React here: **`useEffect(effect)` takes no deps array
and runs after *every* render**, not once on mount (the effect is re-queued each
render; see [components.md](components.md)). So `useEffect` is the wrong tool for
a once-per-mount reset — it would stomp the flag mid-interaction. The once-on-
mount idiom in yui is a `useRef` init guard: a ref persists across a fiber's
renders and is fresh on a remount, so a check-and-set runs exactly once per
mount.

```cpp
// reset-once-on-mount via a useRef init guard (transient to THIS lifetime):
bool& inited = ctx.useRef(false);
if (!inited) {
    inited = true;
    store->set([](auto& s){ s.revealHeld = false; });
}
```

Edge-trigger for the live behavior **plus** the mount reset for the leak. The
edge-trigger alone is the 80% recipe; the reset is what makes it correct across
unmount-while-held.

---

## Floating-panel placement

Yoga lays out *within* a panel well. But absolute placement of **floating**
panels — context menus, dropdowns, cascading submenus, tooltips — is geometry
the consumer currently re-derives every time: screen-edge clamping, side-flipping
a submenu relative to its parent, shifting up when a panel would overflow the
bottom, anchoring to a scrolled item's *true drawn position*. A real integration
hit four separate bugs here (right-wall overflow, submenu overlap, tall-menu
clamp, scroll mis-anchor), and the same class of bug was fixed twice in the
library's own examples.

This now ships as **[`yui/layout/Placement.hpp`](../include/yui/layout/Placement.hpp)**
— pure free functions in `namespace yui::layout`, no render types, geometry in /
position out. The consumer wires the result into `.positionLeft()` /
`.positionTop()` on its own panel, keeping the `Box`, the `Scroll`, the backdrop,
and all chrome entirely its own. A wrapper that owned the `Box` was explicitly
rejected: every placement bug was a *geometry* bug, not a structure bug, and a
wrapper would force consumers to adapt their settled panel structure while still
reaching past it for cascade-specific bits. The examples
([context_menu.cpp](../examples/context_menu.cpp)) consume this helper rather
than hand-rolling the math.

### API

```cpp
namespace yui::layout {
struct Rect     { float x, y, w, h; };          // a DRAWN on-screen rect
struct Vec      { float x, y; };
// Per-edge insets, so a host can reserve space asymmetrically (e.g. a menu bar:
// top = BAR_HEIGHT, other edges small). Viewport::uniform(w, h, inset) for the
// common same-on-every-side case.
struct Viewport { float width, height, top = 8, right = 8, bottom = 8, left = 8;
                  static Viewport uniform(float w, float h, float inset = 8); };
enum class Side { Right, Left };

// Vertical "move, don't shrink": shift up at the bottom edge; scroll only when
// taller than the window column.
PlacedY   placePanelY(float anchorY, float contentH, Viewport, float fixedMaxH = 0);

// A free-standing panel at a point (context menu / dropdown): clamp both axes.
PlacedRect placePanel(Vec anchor, Vec panelSize, Viewport, float fixedMaxH = 0);

// Cascading submenu off a parent panel: side-aware flush X (never overlapping)
// + move-don't-shrink Y, in one call. The cascade one-liner.
PlacedRect placeSubmenu(const Rect& parent, float anchorY, Vec panelSize,
                        Viewport, Side prefer = Side::Right, float fixedMaxH = 0);

// Just the side decision (flush X), if you place Y yourself.
float chooseSideX(const Rect& parent, float panelW, Viewport, Side prefer = Side::Right);
}
```

The cascade case collapses to one call: `parent =` the parent item's **drawn
rect**, `prefer = Side::Right`, and `placeSubmenu` does
flip-if-no-fit + flush-X + shift-up-at-bottom internally.

### Three constraints, each earned the hard way — and how the API enforces them

1. **Anchor is a drawn-position rect, not a logical index/offset.** The worst
   real bug was reconstructing the anchor from content-offset
   (`placedPanelY + offsetY`) instead of the item's true rendered Y — so a
   scrolled parent mis-anchored its submenu. `placeSubmenu` takes the parent's
   `Rect` (its **actual rendered** position) and the row's drawn `anchorY`, which
   *forces* the consumer into the correct value and makes that bug class
   unrepresentable. In a scrollable menu, anchor Y to the parent row's drawn
   position (e.g. the cursor Y on hover), never to a content offset.

2. **Side-selection and clamping are ONE decision, not two passes.** The nastiest
   bug was not a single wrong formula — it was a flip pass and a clamp pass that
   each looked correct alone but **composed into an overlap near the left edge**.
   `chooseSideX` decides the side AND yields the final flush X in one step
   (prefer the fitting side; if neither fits, the side with more room, flush —
   never clamped forward over the parent). `placePanel` only backstops the
   free-panel case onto the screen. The two are never split.

3. **No yui-type coupling.** Pure functions over plain `Rect` / `Vec` /
   `Viewport`. Unit-tested in isolation
   ([test_placement.cpp](../test/test_placement.cpp)), composable, and the
   consumer owns the wiring into its layout. This is the layer integrators kept
   hand-rolling and getting wrong — now a tested function, not a framework.

---

## What works (so the list isn't all edges)

- The **dirty-flag / reconcile model** does exactly what it promises: once state
  is in a `Store`, live updates "just work" with no flicker. Migrating a menu
  from feeling immediate-mode to properly reactive was a mechanical change once
  the state was in the right container.
- The **cross-thread `set()` contract** is clean — one sanctioned entry point,
  deferred re-render on the host thread.
- The reactivity is genuine React-shaped reactivity; the gaps above are all in
  discoverability of the edges, not in the core model.
