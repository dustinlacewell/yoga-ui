# Changelog

## 1.1.1

Patch release: text-measurement correctness under embedders, and a packaging fix.

- **Text measurement ignores the active NanoVG transform.** `NvgRenderer::measureRun`
  neutralizes the current transform before calling `nvgTextBounds`, so a host with
  an active zoom/scroll transform during paint (e.g. a VCV Rack module) measures
  runs identically at layout and paint time.
- **Measured text sizes ceil to the pixel grid.** `ITextMeasurer::measure` and
  `fallbackMeasure` round their result up to integer pixels, so Yoga's edge
  rounding can't land a content box narrower than the text it was wrapped for.
- **`yui.mk` now lists the `src/render/*.cpp` sources.** The 1.1.0 fragment
  omitted `Measure.cpp`, `StyleResolver.cpp`, `TextWrap.cpp`, and
  `TreeRenderer.cpp`, so Makefile-based consumers (e.g. VCV Rack plugins)
  failed to link against 1.1.0.

## 1.1.0

The interaction-layer release. Inputs, focus, overlays, and a widget set — plus
a batch of correctness fixes. **This release contains breaking changes** (a few
removed/reshaped APIs); see [`docs/migration-1.0-to-1.1.md`](docs/migration-1.0-to-1.1.md)
for the exhaustive upgrade guide.

### Highlights

- **A standard widget set** (`yui::widgets`, header-only): `Button`, `Checkbox`,
  `Switch`, `RadioGroup`, `Progress`, `Slider`, `Tabs`, `Select`. All controlled
  (value + `onChange`), with opt-in keyboard nav and override-able chrome.
- **The Portal overlay model** — detached content rendered at root z-order, the
  basis for `Modal` (scrim + focus trap) and `Tooltip` (hover-delay), plus the
  `Select` dropdown and the migrated menu examples.
- **Full text editing** — caret, selection (keyboard, drag, double/triple-click),
  multiline with vertical navigation, password masking per code point, and a
  clipboard seam (`IClipboard`) with cut/copy/paste. Driven by an `EditCommand`
  enum the platform pump maps keycodes to, routed through a new 5-arg
  `handleKeyDown` so a focused input owns its editing keys.
- **Pointer capture + drag model** — implicit capture (moves route to the press
  target, release delivered off-node), an `onDrag` stream, and draggable overlay
  scrollbars with a programmatic scroll API (`scrollTo`/`scrollIntoView`).
- **Focus generalized beyond inputs** — `.focusable()`, `.autoFocus()`,
  document-order Tab traversal, and focus traps.
- **Element refs** — `useElementRef()` + `.ref()` + `getBoundingRect()`, the
  React `useRef`/`getBoundingClientRect` analog for anchoring floating UI.
- **A backend-neutral render seam** — `render::IRenderBackend` + `TreeRenderer`
  walk the tree; backends supply only drawing primitives. Text measurement rebuilt
  on `measureRun`/`fontMetrics` so measure and paint share one wrap algorithm.

### Breaking (see the migration guide)

- `Host::handleBackspace()` / `handleSubmit()` removed — fold into `EditCommand`.
- `onKeyDown` callbacks take a third `bool repeat`; `onMouseDown` takes
  `(float, float, MouseButton, uint16_t)` (was zero-arg).
- `onClick` requires press and release on the same node (⚠️ silent).
- `useRef<T>` no longer dangles across a later hook (source-compatible fix);
  `Store::use()`/`peek()` return by value, not `const T&`.
- Custom `ITextMeasurer`/renderers must implement the new pure virtuals and
  `std::string_view` font params (bundled NanoVG/SDL backends already do).

### Fixed

- `useRef` returned a reference into the reallocatable hook-state vector — a
  use-after-free when a later hook grew it. Now heap-stable.
- `Select` dismissed its popup on the option click's blur before the selection
  committed. Backspace deletes a UTF-8 code point, not a byte. Multiline arrow
  up/down navigate by line in the showcase shims. Store thread-safety hardened.

## 1.0.0

First stable release. The public API surface and ABI are now deliberately
locked; subsequent 1.x releases keep source/ABI compatibility.

### Highlights

- **Per-primitive builders.** `Box()`, `Text()`, `Input()`, `Scroll()`, and
  `Canvas()` now return distinct builder types that expose only the setters
  valid for that primitive. Misusing a primitive — `Text("x").value("y")`,
  `Scroll(c).onChange(...)` — is now a **compile error** instead of a runtime
  `std::bad_variant_access` (or a silent no-op). Builders implicitly convert to
  `VNode`/`Child`, so composition (`Row({...})`, returns from helpers and
  components, `setRender`) is unchanged.
- **Stated, enforced threading contract.** yui is single-threaded; the Host,
  events, reconciliation, and rendering run on one thread. `Store::set()` is the
  only sanctioned cross-thread entry point — it marks dirty flags (now atomic)
  and the re-render is applied on the host thread at the next `update()`. The
  contract is documented in `Host.hpp`/`Store.hpp`.
- **Diagnosable failure modes.** `Host::update()` reports *why* it no-op'd via
  `UpdateResult::status` (`NoRenderFn` / `EmptyRender` / `ZeroViewport` /
  `Reentrant`). Hook misuse (type change or reorder at a stable index) and
  `Store::set()` during render are reported through the `ErrorHandler` sink
  instead of throwing a bare exception or livelocking silently.
- **Locked 1.0 header surface.** The reconciliation algorithm moved to a
  non-installed `include/yui/detail/`; `Host` holds it via pimpl. Installed
  headers expose only the real API. A narrow `DirtyScheduler` interface breaks
  the core↔Host include cycle and makes the core headless-testable.

### Fixes

- Keyboard events route by phase: a node with only `onKeyUp` now receives
  `KeyUp` (previously unreachable).
- `update()` is guarded against reentrancy from within a callback (was tree
  corruption; now ignored + diagnosed).
- `Store::set()` no longer holds its lock across user code, so a reentrant
  `use`/`peek`/`set` from a subscriber can't self-deadlock.
- Focused-`Input` liveness is validated before every dereference (fixes a
  use-after-free when the focused Input is reconciled away).
- Fixed an out-of-bounds `hookState` access when a stateful hook (`useState`/
  `useRef`) followed a `useEffect` or `useField` in a component.
- Backend parity: SDL and NanoVG now share one set of visual defaults
  (`RenderDefaults.hpp`), the SDL focus caret matches NanoVG, and the text
  measure fallback and SDL paint size agree on font size **14** (was 12 vs 14,
  which caused clipping).
- `createNode` / renderer `switch`es handle an out-of-range `PrimitiveType`
  loudly instead of null-dereferencing or drawing nothing.
- Event-path walks (hit-test, dispatch, key routing) are depth-bounded and
  degrade gracefully on pathologically deep trees.

### Notes for upgraders (source compatibility)

These are the only ways idiomatic pre-1.0 code may need a touch-up:

- **Multi-branch `auto`-returning component lambdas** that mix a builder branch
  (`return Box()...;`) with a `VNode` branch (`return Text()...;` or
  `VNode::empty()`) now need an explicit `-> VNode` return type, because the
  branches return different builder types. Single-expression returns and lambdas
  already annotated `-> VNode` are unaffected.
- **`useField` now counts as a hook** for rules-of-hooks ordering (it previously
  advanced no hook index). Don't place a positionally-following hook in a
  different order across renders relative to a `useField`.
- Wrong-primitive setter calls that previously compiled (and threw at runtime)
  no longer compile — this surfaces latent bugs at build time.
