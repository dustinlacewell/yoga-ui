# Migration guide: YUI post-1.0 (v1.0.0 → current)

This guide covers every consumer-visible change between the **v1.0.0** tag and the
current `master`. It is written for projects that vendor YUI as a git submodule
and are bumping it forward across the interaction-layer campaign (pointer
capture, focus/Tab, text editing + selection + clipboard, the Portal overlay
model, the backend-neutral render seam, the installed widget set, and a set of
correctness fixes).

## TL;DR — the things that will actually break your build or behavior

1. **Custom `ITextMeasurer` / renderer → hard compile break.** New pure virtuals
   (`measureRun`, `fontMetrics`) and `std::string_view` font params. You must
   implement them. See *The render + text-measurement seam*.
2. **`onKeyDown` callback signature gained a `bool repeat` param.** Existing
   handlers must add the parameter. See *Events, focus, and the Host loop*.
3. **`onClick` now requires press AND release on the same node** — ⚠️ a *silent*
   behavior change (compiles, behaves differently).
4. **`Host::handleKeyDown` has a new 5-arg routing overload** your platform event
   pump should adopt so a focused input owns its editing keys.
5. **`useRef<T>` dangling-reference fix** — source-compatible, but if you had
   `useRef` followed by another hook you were reading freed memory before. ⚠️
6. **`Store::use()` returns a copy** — holding `const auto& s = store.use()` and
   reading a member after a `set()` is a lifetime bug.

Everything else is **additive** or source-compatible: you opt in when you want it
(widgets, Portal, Modal/Tooltip, element refs, text selection/clipboard,
programmatic scroll), and the factory children went brace-list → variadic
(`Row({a,b})` → `Row(a,b)`) without breaking the old form. See *Factories*.

---

## How to bump the submodule

```bash
cd <your-project>/<path-to>/yui      # e.g. plugin/dep/yui  or  dep/yui
git fetch origin
git checkout master                  # or a specific commit/tag
cd -                                 # back to your project root
git add <path-to>/yui
git commit -m "Bump yui to <sha>"
```

Then do a **clean rebuild** — the render/measure seam changes mean an
incremental build can link stale object files against new vtables.

---

## Events, focus, and the Host loop

### 1. `onKeyDown` callbacks take a third `bool repeat` parameter

**What changed:** The builder setter's callback type gained a key-repeat flag (commit `8e3c321`).

```cpp
// v1.0.0
Derived& onKeyDown(std::function<void(int, uint16_t)> fn);
// HEAD
Derived& onKeyDown(std::function<void(int, uint16_t, bool)> fn);   // (keyCode, mods, repeat)
```

**What a consumer must do:** Every existing two-param `onKeyDown` lambda is a **compile error** (a `(int, uint16_t)` lambda does not convert to `std::function<void(int, uint16_t, bool)>`). Add the third parameter:

```cpp
// before
.onKeyDown([](int key, uint16_t mods) { ... })
// after
.onKeyDown([](int key, uint16_t mods, bool repeat) { ... })
```

`repeat` is `true` for OS key-repeat events. Note the Host side is unchanged: `Host::handleKeyDown(int, uint16_t, bool repeat = false)` already existed at v1.0.0 — the flag now actually reaches your handlers. If your shim passes the platform's repeat flag through, held keys now report `repeat = true`; decide per-handler whether to act on repeats.

### 2. ⚠️ SILENT — `onClick` now requires press AND release on the same node

**What changed:** At v1.0.0, `onClick`/`onRightClick`/`onMiddleClick` fired on any `MouseUp` over the handler node, regardless of where the press landed. Now a click fires only when the press target is within the handler node's subtree (commit `9ed355d`). A release whose press landed elsewhere — the classic "clicking A opens overlay B under the cursor, B's onClick fires spuriously" — now fires nothing. Press-on-child + handler-on-parent still clicks.

**What a consumer must do:** No compile error — this is a runtime semantics change.
- If you relied on `handleMouseUp` alone triggering clicks (tests, synthetic input, automation), you must now send a matching `handleMouseDown` first. The library's own tests were updated exactly this way.
- If you had workarounds for the spurious/orphan-click bug, delete them.
- Additionally (commit `5829b9c`): once a press moves past the drag threshold (`render_defaults::kDragThresholdPx = 4.0f`), the release **does not** fire `onClick` at all — drags no longer count as clicks.

### 3. Mouse handlers: new signatures + implicit pointer capture

**What changed:** `onMouseDown` changed shape; `onMouseUp`, `onMouseMove`, and `onDrag` are new; and the event layer now implements implicit pointer capture (commit `5829b9c`).

```cpp
// v1.0.0
Derived& onMouseDown(std::function<void()> fn);
// onMouseUp / onMouseMove / onDrag did not exist

// HEAD
Derived& onMouseDown(std::function<void(float, float, MouseButton, uint16_t)> fn); // x, y, button, mods — fires on PRESS
Derived& onMouseUp  (std::function<void(float, float, MouseButton)> fn);           // x, y, button — fires on RELEASE
Derived& onMouseMove(std::function<void(float, float)> fn);                        // x, y
Derived& onDrag     (std::function<void(const DragEvent&)> fn);
```

**⚠️ SILENT capture semantics:** while a button is held, the press target holds implicit pointer capture:
- `onMouseMove` routes to the **captor** while pressed (the hovered node otherwise) — a node no longer sees moves while another node's press is held.
- `onMouseUp` is delivered to the **press target** even when the pointer has moved off it or off-window.
- Presses on a Scroll node's overlay scrollbar are consumed as chrome (thumb drag / track page) with **no user-handler dispatch, focus change, or click chaining** (commit `8938c4e`).

**`onDrag`:** once the pointer moves past `kDragThresholdPx` from the press anchor, a `DragEvent` stream is delivered to the captor (`{x, y, dx, dy, startX, startY, button}`).

**What a consumer must do:** Fix `onMouseDown` call sites (compile error — zero-arg lambdas no longer convert); take `(float, float, MouseButton, uint16_t)`. Replace any hand-rolled drag tracking with `onDrag`. `Host::handleMouseDown` also gained a defaulted `uint16_t mods = KeyMod_None` — old 3-arg calls still compile, but pass the live modifier mask or shift-click selection in inputs won't work.

### 4. `Host::handleKeyDown` — new 5-arg routing overload

**What changed:** A second overload owns full keydown priority (commit `b9712e1`). The 3-arg overload still exists, unchanged.

```cpp
bool handleKeyDown(int keyCode, uint16_t keyMod, bool repeat = false) noexcept;           // app dispatch only
bool handleKeyDown(int keyCode, uint16_t mods, bool repeat,
                   std::optional<EditCommand> edit, bool focusNav = false) noexcept;      // full routing
```

The 5-arg overload routes in priority order: (1) if `edit` is set and the focused Input consumes it, done — app handlers never see it; (2) if `focusNav` (the platform's Tab), run `focusPrev()`/`focusNext()` per Shift — unswallowable; (3) fall through to app `onKeyDown` dispatch. `edit = std::nullopt` means "not an editing key"; the shim owns the keycode→`EditCommand` mapping (core stays keycode-agnostic — SDL Tab is 9, GLFW's is 258).

**What a consumer must do:** Platform pumps should switch to the 5-arg overload: map arrows/Home/End/Backspace/Delete/Ctrl-A/X/C/V/Enter to `EditCommand` values and Tab to `focusNav = true`, then call it once per keydown. If you keep calling the 3-arg overload, editing keys stop reaching focused inputs ahead of app handlers and Tab traversal never runs.

### 5. ⚠️ SILENT — `Host::update()` result bits mean different things now

**What changed:** Signature identical to v1.0.0 — `UpdateResult update(float width, float height, float dt = 1.f/60.f) noexcept` — but the composition of `needsRepaint` changed (commit `568fa66`): it now also sets on animation frames that actually changed pixels, on hover/press/focus transitions, and on dirt carried from deferred handler updates.

**What a consumer must do:** Nothing if you repaint unconditionally. If you gate rendering on `needsRepaint`:
- Hover/press/focus visual transitions now set it (you can stop repainting-every-frame to fix stale highlights).
- `animating == true` no longer forces `needsRepaint` (a blinking caret "animates" every frame but only dirties on visible/hidden edges) — use `animating` to keep the loop hot, `needsRepaint` to decide whether to draw.
- A new `UpdateStatus::Deferred` enum value exists (next item); exhaustive switches need a case.
- Pass your real frame `dt` — the multi-click clock advances on it, so double-click timing assumes 60fps otherwise.

### 6. ⚠️ SILENT — handler-triggered `update()` is deferred to end of dispatch

**What changed:** Calling `host.update()` from inside an event callback is now legal: it returns `UpdateStatus::Deferred`, is coalesced, and applied **same-frame** once the outermost `handle*()` unwinds — React-style batching (commit `6415f19`).

**What a consumer must do:** No compile error. Inside the handler the reconcile has **not** happened when `update()` returns — `host.root()` still shows the pre-update tree until the `handle*()` call returns. The deferred update's repaint/layout/animating bits are OR-folded into the next explicit `update()`'s result.

### 7. Focus generalized beyond Input (mostly additive)

Focus is no longer Input-only (commit `5422478`). No node silently became focusable — Inputs always were; everything else opts in via `.focusable(bool=true)` (click + Tab) / `.autoFocus(bool=true)` (focus on mount). New Host API (additive): `focus(Node*)`, `blur()`, `focusNext()`/`focusPrev()` (document-order Tab, wrapping), `setFocusTrap(Node*)`/`clearFocusTrap()`, `getFocusedNode()`. `getFocusedInput()` still exists. **One relocation:** `InputBuilder::autoFocus` moved to the generic builder prop — `Input().autoFocus()` still compiles (now writes `EventProps`), no action unless you touched `InputProps::autoFocus` directly.

### 8. Scrolling (additive)

`NodeRef::asScroll()` returns the live `ScrollNode*`, exposing `scrollTo(x,y)` (clamped, smooth) and `scrollIntoView(rect)`. Scroll nodes now render draggable overlay scrollbars (handled as chrome — see §3 capture caveat). `onScroll(void(float,float))` is unchanged.

### 9. Clipboard seam (additive)

`Host::setClipboard(IClipboard*)` installs a platform clipboard (`Clipboard.hpp`: `getText()`/`setText()`, lifetime self-managed like `setTextMeasurer`). `EditCommand::Cut/Copy/Paste` route through it; with none installed they report unconsumed. `handleTextInput(const std::string&)` is unchanged.

### 10. Small additive event APIs

- `.onDoubleClick(void())` — fires on the second chained click.
- `.onHoverDelay(void())` + `.hoverDelayMs(float)` — fires once after continuous hover (default 500 ms); the tooltip seam.
- `.cursor(CursorShape)` + `Host::getCursor()` — declare a pointer shape; poll `getCursor()` each frame (captor chain wins during drag, else hovered; Inputs default to IBeam).

## State, hooks, and element refs

### `useRef<T>` — dangling-reference fix (rewrite of the storage slot)

- **What changed:** `useRef<T>` no longer returns a `T&` into `fiber->hookState` (a `std::vector<std::any>`). The slot now holds a `std::shared_ptr<T>` and the hook returns `*shared_ptr` — a reference into stable heap the vector can never move.
- **Old → New:**
  ```cpp
  // v1.0.0 — slot holds a bare T; the returned T& points INTO the vector
  fiber_->hookState.push_back(std::move(initial));
  return std::any_cast<T&>(fiber_->hookState[index]);

  // HEAD — slot holds shared_ptr<T>; the returned T& points at the heap
  fiber_->hookState.push_back(std::make_shared<T>(std::move(initial)));
  return *std::any_cast<const std::shared_ptr<T>&>(fiber_->hookState[index]);
  ```
- **What a consumer must do:** Nothing — the return spelling is still `T&`, so `int& r = ctx.useRef<int>(0);` and `auto& r = ctx.useRef<T>(...)` compile unchanged. Every call site is source-compatible.
- ⚠️ **SILENT (behavioral fix):** at v1.0.0, any component that called `useRef` and then any later push-backing hook **in the same render** (`useState`, another `useRef`, `useElementRef`) could have that later hook's `push_back` reallocate `hookState`, dangling the `T&` you already held. A captured lambda then read/wrote **freed memory**:
  ```cpp
  auto& count = ctx.useRef<int>(0);                    // T& into the vector
  auto [text, setText] = ctx.useState(std::string{});  // push_back may realloc → count now dangles
  return button("hit").onClick([&count]{ count++; });  // UAF at v1.0.0
  ```
  At HEAD this is correct. If your v1.0.0 app had `useRef` anywhere but the *last* hook in a component and "worked", it worked by allocator luck — upgrading makes intermittent corruption around ref'd values disappear. (Minor: the rules-of-hooks tag for a ref slot is now `typeid(std::shared_ptr<T>)`; invisible unless you were poking `fiber()->hookState` directly.)

### `Store<T>::use()` / `peek()` — return by value, not `const T&`

- **What changed:** both return `T` **by value** (a copy taken under the store mutex); v1.0.0 returned `const T&` into the store's internal `value_`.
  ```cpp
  const T& use() const;   // v1.0.0 — reference into value_
  T use() const;          // HEAD  — snapshot copy under the lock
  ```
  A returned reference dangles the instant a cross-thread `set()` reassigns `value_`; the copy is what makes the read race-free.
- **What a consumer must do:** `auto s = store.use();` and `const auto& s = store.use();` both still compile (the `const&` lifetime-extends the temporary). **Must change:** anything taking the *address* of the result expecting it to track the store — `const AppState* p = &store.use();` now points at a dead temporary.
- ⚠️ **SILENT:** the value is now a **snapshot** at the `use()` call. At v1.0.0, `const auto& s = store.use();` then reading `s.field` after a later `set()` saw the *new* value (live reference); at HEAD you keep reading the copy. If you relied on live read-through, re-call `use()`/`peek()` after the `set()`. Also: a large `T` now costs a copy per `use()`.

### `Store<T>::set()` — thread-safety hardened, same signatures

- **What changed:** `set(T value)` and `set(const std::function<void(T&)>&)` keep their signatures; `Store::set()` is now genuinely the one sanctioned cross-thread write path (fiber subscribers liveness-checked under the mutex, TOCTOU-free host marking, release/acquire dirty handoff). Self-`set()` from inside a mutator on the same store is detected and diagnosed rather than deadlocking; a `set()` during render still applies but emits a one-shot "will re-render every frame" diagnostic.
- **What a consumer must do:** nothing for well-formed code. Restated contract: `use()`/`peek()` are **host-thread-only**; `Store::set()` is the *only* API you may call cross-thread. ⚠️ The `useState` setter is **host-thread-only** — if you called one from a worker/timer thread at v1.0.0 (it appeared to work), route cross-thread writes through a `Store` instead.
- (additive) `Store::aliveToken() -> std::weak_ptr<bool>` — liveness token for callbacks that may outlive the store.

### `useField` — field by value; setter liveness-guarded

```cpp
// HEAD
std::pair<T, std::function<void(const T&)>> useField(Store<S>& store, T S::*field);
```
v1.0.0 returned `std::pair<const T&, ...>`; the field is now read **by value** out of the `use()` copy. `auto [value, setValue] = ctx.useField(store, &S::field);` is unchanged; if you spelled the pair type explicitly with `const T&`, update it to `T`. (additive: the setter captures `aliveToken()`, so calling it after the Store is destroyed is a safe no-op.)

### Element refs (additive — new capability)

- `ctx.useElementRef() -> NodeRef` — a hook (occupies a hook slot). Returns the **same** underlying slot every render (React `useRef` identity). Bind **by value** (`auto anchor = ctx.useElementRef();`), not `auto&`.
- `.ref(nodeRef)` on any builder — attaches the handle to that element's render `Node` (host elements only; a ref on a `Component` is a compile error).
- `nodeRef.current() -> Node*` — the live node, or `nullptr` when unmounted, reconciled away, **or during render** (refs deliberately read null in a component body — read them in handlers/effects only, always null-check).
- `nodeRef.getBoundingRect() -> layout::Rect` — absolute drawn rect (scroll-corrected), the `getBoundingClientRect` analog.
- `nodeRef.asScroll() -> ScrollNode*` — for the programmatic scroll API.
- Free function `yui::absoluteRect(const Node*) -> layout::Rect` — same walk for any `Node*` you hold.

```cpp
auto anchor = ctx.useElementRef();
return Box(Button("Open").ref(anchor).onClick([anchor, setPos]{   // handler, not render
    if (Node* n = anchor.current()) {                            // always guard
        layout::Rect r = anchor.getBoundingRect();
        setPos({r.x, r.bottom()});                               // popover below the button
    }
}));
```

## The render + text-measurement seam (custom backends)

> ⚠️ **BREAKING for any custom `ITextMeasurer` or custom renderer.** New pure virtuals (`measureRun`, `fontMetrics`), the old `measure` made non-virtual, and `std::string_view` font params. A v1.0.0 subclass fails to compile on three counts (unimplemented pure virtuals, an `override` on a no-longer-virtual method, mismatched signatures). **Note: if you use the *bundled* `nvg::NvgRenderer` or `sdl::SdlRenderer` as your measurer/renderer (both target projects do), you are NOT affected — they already implement all of this. This section is for projects that authored their own.**

### 1. `ITextMeasurer`: implement two primitives instead of `measure`

```cpp
// v1.0.0 — one pure virtual
virtual Size measure(const std::string& text, float fontSize, float maxWidth) const = 0;

// HEAD — these two are the pure virtuals; measure() is now concrete (library-provided)
virtual float measureRun(std::string_view run, float fontSize, std::string_view font) const = 0;
virtual FontMetrics fontMetrics(float fontSize, std::string_view font) const = 0;
```

`FontMetrics` (pixels, **descent positive** — flip NanoVG's sign):
```cpp
struct FontMetrics { float ascent; float descent; float lineHeight; };
```

A NanoVG-backed migration:
```cpp
float measureRun(std::string_view run, float fontSize, std::string_view font) const override {
    nvgFontSize(vg_, fontSize); selectFace(font);
    return nvgTextBounds(vg_, 0, 0, run.data(), run.data() + run.size(), nullptr);
}
FontMetrics fontMetrics(float fontSize, std::string_view font) const override {
    nvgFontSize(vg_, fontSize); selectFace(font);
    float a, d, l; nvgTextMetrics(vg_, &a, &d, &l);
    return {a, -d, l};   // descent flipped positive
}
```
Callers of `measure` keep working (it still exists), but wrapping now comes from the shared algorithm, so line breaks may differ from your old override.

### 2. Font parameters are now `std::string_view`

Font-name params across measure/draw went from `const std::string&` to `std::string_view` (commit `ac82ca7`) — an override taking `const std::string& font` will "does not override". In `drawTextRun`, the **run** stays `const std::string&` (SDL_ttf needs null-terminated UTF-8); only the **font name** is a view. If your font registry keys on `std::string`, use transparent hash/equality or construct a temporary `std::string(font)`.

### 3. `render::IRenderBackend` — the backend-neutral walk (optional, recommended)

A new `yui/render/` layer extracts the node-type switch, style cascade, text wrapping, scroll clip/offset, input chrome, and caret into `render::renderTree(Node*, IRenderBackend&, const ErrorHandler&) noexcept`. Backends supply only primitives. **Optional** — a hand-walking renderer keeps compiling — but hand-walking means re-implementing selection/multiline/caret yourself. `IRenderBackend` **inherits `ITextMeasurer`** (one class does both) and is handed **absolute** coords:

```cpp
struct Rect { float x = 0, y = 0, w = 0, h = 0; };  // yui::render::Rect, absolute

class IRenderBackend : public ITextMeasurer {
public:
    virtual void beginFrame() = 0;
    virtual void endFrame()   = 0;   // restore embedder-observable state, incl. a clip stack
    virtual void fillRect  (const Rect&, uint32_t color, float radius) = 0;   // color 0xRRGGBBAA
    virtual void strokeRect(const Rect&, uint32_t color, float radius, float width) = 0;
    virtual void pushClip  (const Rect&, float radius) = 0;   // intersects current; nests
    virtual void popClip   () = 0;
    virtual void drawTextRun(const std::string& run, float x, float y,
                             float fontSize, uint32_t color, std::string_view font) = 0;
    virtual void drawCanvas(const CanvasNode&, const Rect&) = 0;   // origin at (r.x, r.y)
};
```
Bracket the walk yourself: `backend.beginFrame(); render::renderTree(root, backend, onError); backend.endFrame();`. The bundled `NvgRenderer` is the reference implementation (font registry + `selectFont` shared by draw and measure so the two never diverge). `render::wrapText`/`runsSize` (`render/TextWrap.hpp`) and the pure `resolveBox`/`resolveText`/`resolveInput` (`render/StyleResolver.hpp`) are public if you adopt pieces incrementally.

### 4. Per-node named fonts + embedder contract (additive)

`Text`/`Input` gained `.font("name")` (resolved against the renderer's registered faces; empty = default) — this is *why* the font param threads through every measure/draw signature. If you embed a bundled renderer, after `render()` returns backend state is exactly as on entry (`NvgRenderer` brackets in one `nvgSave`/`nvgRestore`; `SdlRenderer` restores draw color/blend/clip/target and clamps geometry) — no need to re-assert your own draw state.

## Factories: variadic children (source-compatible)

**What changed:** `Box`/`Row`/`Column`/`Scroll` (and the new `Portal`) take children
as a **variadic pack** instead of a brace-list, and the old single-child
`Box(VNode)` / `Box(Component)` / `Scroll(VNode)` / `Scroll(Component)` overloads
were removed in favor of it (commit `40e6602`, to avoid a per-render subtree copy).

```cpp
// v1.0.0 style
Row({ Text("a"), Box({ Text("b") }), Column({ Text("c"), Text("d") }) });

// HEAD style — drop the braces
Row(Text("a"), Box(Text("b")), Column(Text("c"), Text("d")));
```

**What a consumer must do: nothing.** The `std::vector<Child>` overload was kept,
so `Row({a, b})` brace-lists still compile (they bind to the vector overload), and
single-child `Box(child)` binds to the variadic. Verified: every old call shape in
both projects still builds. Two notes:
- The factories are now `[[nodiscard]]` — if you ever call `Box(...)` for effect
  and discard the builder, you'll get a new unused-result **warning** (not an error).
- A runtime-built child list still uses the vector form: `Box(std::move(myVec))`.
  Only the *static* brace-list is what the variadic form replaces.

Adopting the brace-free form is optional cleanup, not a required migration.

## The overlay model: Portal, Modal, Tooltip (additive)

A new **Portal** primitive renders detached content at root z-order — the basis
for menus, modals, tooltips, and dropdowns that must escape ancestor clipping
and hit-test above the main tree.

```cpp
// Content laid out against the viewport, painted + hit-tested at root z-order.
// It reconciles in its LOGICAL parent (no reparenting), so no stored Node
// pointer is ever held — a portal can't dangle.
Portal(myOverlayContent)

Portal(content).trapFocus()   // scope Tab to the portal; save/restore focus
                              // across mount/unmount (the Modal contract)
```

Two widgets compose over it, both header-only in `yui::widgets`:

```cpp
#include <yui/yui.hpp>   // umbrella pulls in all widgets

// Modal: dialog over a scrim. Open/close is app state — render it while open,
// stop rendering it to close. Backdrop click / dismiss key call onDismiss.
widgets::Modal(dialogContent)
    .onDismiss([]{ /* app drops the modal from the next render */ })
    .dismissOnBackdropClick(true)
    .dismissKeyCode(myEscKeyCode)      // app-supplied (core is keycode-agnostic)
    .backdropColor(0x00000088u);

// Tooltip: wraps a target, shows a floating tip after a hover delay.
widgets::Tooltip(targetVNode)
    .tip("Helpful text")               // or .tip(anyChild)
    .delayMs(500.0f)
    .tipSize(160, 40);                 // optional, for edge-clamping
```

Placement for floating panels is available directly (`yui/layout/Placement.hpp`):

```cpp
auto vp     = layout::Viewport::uniform(rootW, rootH);
auto placed = layout::placePanel({anchor.x, anchor.bottom()},   // Vec anchor
                                 {panelW, panelH},              // Vec panelSize
                                 vp,
                                 maxHeight /*=0 for none*/);     // PlacedRect{x,y,height}
```

`placePanel` anchors below and shifts up (move-don't-shrink) near the bottom
edge; `placed.height` is the clamped height for a scrolling panel.

---

## Text editing, selection, and clipboard (additive)

Inputs gained a full editing model: a caret (byte offset on a code-point
boundary), selection, multiline, and clipboard — driven by an `EditCommand`
enum the platform pump maps raw keycodes to.

```cpp
enum class EditCommand {
    MoveLeft, MoveRight, MoveLineStart, MoveLineEnd, MoveUp, MoveDown,
    SelectAll, DeleteBackward, DeleteForward, Cut, Copy, Paste, InsertNewline,
};
```

**The shim you write** (raw keycode → `EditCommand`) is the untested boundary —
map your toolkit's codes. Note `MoveUp`/`MoveDown` are what drive **multiline**
vertical navigation; a shim that omits them means arrow-up/down does nothing in
a multiline input. Route via the new 5-arg `handleKeyDown` (see *Events*), which
lets a focused input consume its editing keys before app handlers see them, and
falls through for mode-inapplicable commands (e.g. `MoveUp` on a single-line
input).

```cpp
Input().value(v).onChange(setV)
    .multiline()          // Enter inserts '\n'; box grows with wrapped lines
    .password(true)       // masks per code point
    .autoFocus();
```

Clipboard is a seam (`IClipboard`, mirroring `ITextMeasurer`'s liveness
discipline) you install on the Host:

```cpp
host.setClipboard(&myClipboard);   // Cut/Copy/Paste EditCommands route through it
```

The bundled SDL backend ships `sdl::SdlClipboard`; a NanoVG/embedder app
provides its own (a few lines wrapping the host clipboard).

---

## The installed widget set (additive)

Eight header-only widgets in `yui::widgets`, all **controlled** (you pass the
value + an `onChange`; the app owns state). Keyboard nav is opt-in via
app-supplied keycode setters (no handler is attached unless you set a keycode,
so a focused widget never black-holes app keys).

```cpp
widgets::Button("Label").onClick(fn).disabled(false);
widgets::Checkbox(checked).label("Accept").onChange([](bool v){ ... });
widgets::Switch(on).label("Enable").onChange([](bool v){ ... });         // sliding pill
widgets::RadioGroup({"Free","Pro","Team"}).value(i).onChange([](int i){ ... });
widgets::Progress(0.6f);                                                 // visual only
widgets::Slider(0.6f).onChange([](float v){ ... }).step(0.01f);          // pointer-capture drag
widgets::Tabs().active(i).tab("A", panelA).tab("B", panelB).onChange([](int i){ ... });
widgets::Select({"Apple","Banana"}).value(i).placeholder("Pick...").onChange([](int i){ ... });
```

Each has color/size override setters (defaults are a dark theme) and, where it
makes sense, keyboard setters: `Checkbox/Switch.toggleKeyCode`, `Tabs.prev/
nextKeyCode`, `Slider.increment/decrementKeyCode`, `Select.up/down/select/
dismissKeyCode`. `Slider` and `Select` are stateful components (they hold an
element ref / open state internally); the rest are pure builders. `Tabs`
remounts panels on switch (inactive panel state is not retained — lift it to a
Store if you need persistence).

---

## Consumer-specific: what actually breaks in your projects

Both `vcv-multiplayer` and `vcv-prefabs` sit at ~v1.0.0 and integrate YUI the same
way: a `YuiHost : rack::widget::OpaqueWidget, yui::Host` that forwards Rack's
`onSelectKey`/`onSelectText`/mouse events into `Host::handle*`, with a bundled
`nvg::NvgRenderer` used as both renderer and text measurer. That shared shape
means a shared, short break list — plus a few prefabs-only items.

### Breaks BOTH projects

**1. `Host::handleBackspace()` and `Host::handleSubmit()` were removed.** ⛔ hard
compile break. Your `YuiHost::onSelectKey` calls both. They folded into the
`EditCommand` model. Replace:

```cpp
// OLD (YuiHost.cpp onSelectKey):
if (e.key == GLFW_KEY_BACKSPACE) { handleBackspace(); e.consume(this); }
else if (e.key == GLFW_KEY_ENTER) { handleSubmit();    e.consume(this); }

// NEW — route editing keys through EditCommand:
if (e.key == GLFW_KEY_BACKSPACE) { handleEditCommand(yui::EditCommand::DeleteBackward);  e.consume(this); }
else if (e.key == GLFW_KEY_ENTER) { handleEditCommand(yui::EditCommand::InsertNewline);  e.consume(this); }
```

`InsertNewline` fires the focused input's `.onSubmit(fn)` on a single-line input
and inserts `'\n'` on a multiline one — so your existing `.onSubmit` handlers
keep working. **Recommended stronger form**: adopt the new 5-arg `handleKeyDown`
so a focused input owns *all* its editing keys (arrows, Home/End, Delete,
selection, clipboard) — which your current shims don't handle at all. Write a
small `editCommandFor(int glfwKey, uint16_t mods)` returning
`std::optional<EditCommand>` (map Backspace→DeleteBackward, Delete→DeleteForward,
Left/Right→MoveLeft/Right, **Up/Down→MoveUp/MoveDown** — needed for multiline,
Home/End→MoveLineStart/End, Ctrl+A/C/X/V→SelectAll/Copy/Cut/Paste,
Enter→InsertNewline) and call
`handleKeyDown(key, mods, repeat, editCommandFor(key,mods), key==GLFW_KEY_TAB)`.
See the reference shim in `examples/nvg_showcase.cpp`.

**2. `.onMouseDown` lambda now takes four params.** ⛔ hard compile break, **both
projects.** At v1.0.0 the builder took `std::function<void()>`; it now takes
`std::function<void(float, float, MouseButton, uint16_t)>` (x, y, button, mods).
Your zero-arg `.onMouseDown` lambdas (multiplayer `MenuItems.hpp:55,87`; prefabs
`MenuItems.hpp`, `YuiBrowserModal.hpp:839`) won't convert. Since those handlers
read `APP->scene->mousePos` / a `NodeRef` rect internally rather than using the
delivered coords, just widen the signature and ignore the params:

```cpp
// OLD
.onMouseDown([state, rack]() { loadRackAndDrag(state, rack); })
// NEW
.onMouseDown([state, rack](float, float, yui::MouseButton, uint16_t) { loadRackAndDrag(state, rack); })
```

(If you'd rather use the delivered press coords than poll Rack's mouse position,
they're now right there in the first two params.)

**3. Text measurer / renderer — verify, don't panic.** You both pass
`nvg::NvgRenderer` (the *bundled* renderer) to `setTextMeasurer`. The bundled
renderer already implements the new `ITextMeasurer` pure virtuals
(`measureRun`/`fontMetrics`) and the `string_view` font path, so **you have no
custom-measurer break** — this only bites projects that authored their own
`ITextMeasurer`, which neither of you did. Just rebuild clean.

### prefabs-only

**1. `.onKeyDown` lambda now takes three params.** ⛔ compile break at
`YuiMainMenu.hpp:182` / `:192`. The signature is
`std::function<void(int, uint16_t, bool)>` (a `bool repeat` was added). Update:

```cpp
// OLD
.onKeyDown([state](int key, uint16_t) { ... })
// NEW
.onKeyDown([state](int key, uint16_t, bool /*repeat*/) { ... })
```

**2. `eventHandler_.focusInput(nullptr)` no longer compiles.** ⛔ `YuiHost.cpp:158`.
`Host::eventHandler_` is private since the 1.0 ABI lock, and `focusInput` is gone.
Use the public API:

```cpp
// OLD
eventHandler_.focusInput(nullptr);
// NEW
blur();                    // Host::blur() clears focus; or host.focus(node) to set it
```

**3. `useRef` — silently fixed, no edit needed, but important.** All 20 of your
`auto& x = ctx.useRef<T>(...)` sites still compile (the return is still `T&`).
More than that: several of them (`YuiBrowserModal.hpp`, `YuiMainMenu.hpp`,
`YuiFollowLinkModal.hpp`, `YuiLoginModal.hpp`) do `useRef` *followed by another
`useRef`/`useElementRef`* in the same component — which at v1.0.0 meant the
earlier `T&` dangled when the later hook reallocated the hook vector. You were
holding references into freed memory (any intermittent corruption in
browser/login/follow-link modal state is a prime suspect). After the bump these
are correct. No action — but this is the upgrade's biggest latent-bug fix for you.

**4. `Store::use()` now returns by value — audit your `const T&` bindings.** ⚠️
silent. You bind `const br::PatchDetail& detail = detailStore->use();` (and
similar in every async modal). This still compiles (the `const&` lifetime-extends
the returned temporary), but the value is now a **snapshot** taken at the `use()`
call, not a live reference into the store. If any of your render code reads such a
binding *after* calling `set()` on the same store in the same scope expecting the
new value, re-call `use()`. Most of your usages read-then-render without an
intervening `set()`, so they're fine — but grep each modal for a `set()` followed
by a read of a pre-bound `use()` reference.

**5. `ScrollNode` internal-field reads still work.** Your infinite-scroll poll
(`YuiBrowserModal.hpp:1257`) reads `s->contentHeight`, `s->scrollOffsetY`,
`s->layout.height` after a `static_cast<yui::ScrollNode*>`. Those fields still
exist with those names — no break. (It remains an internal-shape dependency; the
supported alternative is `NodeRef::asScroll()`, but you don't have to change it.)

**6. `Placement` API is stable for you.** You use `Viewport`, `placePanelY`,
`placeSubmenu`, `clampRange`, `Side::Right`, `PlacedY` — all present and
unchanged. The `Viewport` aggregate field order your `menuViewport` relies on
(`{width, height, top, right, bottom, left}`) is intact. No action.

### Neither project uses (so you can ignore those sections)

Neither uses the `widgets::` layer, `Portal`, `EditCommand` directly (until you
adopt fix #1's stronger form), `useField`, `setClipboard`, or a custom
`IRenderBackend`/`IClipboard`. The widget set, Modal/Tooltip, and text
selection/clipboard are all available to adopt when you want them.



## Correctness fixes worth knowing (mostly transparent)

These changed behavior for the better; most need no action, but they explain
differences you may notice:

- **Backspace deletes a UTF-8 code point, not a byte** — multi-byte glyphs
  delete cleanly.
- **`Store::set()` is genuinely thread-safe** — `set()` is the sole sanctioned
  cross-thread write path.
- **Components remount on identity change**; effects/cleanups run in a dedicated
  commit phase; a component's self-triggered update is preserved; the commit
  queue is cleared if a reconcile pass throws.
- **Hover enter/leave is cut at the common ancestor** (fewer spurious
  enter/leave events during pointer travel).
- **Overflowing children are hit-tested via a subtree-bounds union** — a child
  drawn outside its parent's box is still clickable.
- **`update()` reports repaint/animation truthfully** through its return value
  (see *Events*), so an embedder can skip redundant repaints.
