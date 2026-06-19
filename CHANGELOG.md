# Changelog

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
