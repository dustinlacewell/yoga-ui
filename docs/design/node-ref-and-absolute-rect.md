# Design Spec: Node Refs + `absoluteRect`

Status: **locked, pre-implementation.** This captures the agreed design so it
survives across sessions and gives the implementation a spec to build against.

## Problem

A consumer building a cascading menu on yui needs to anchor a submenu to the
**stable drawn position** of the parent row it built. Today there is no way to
get that position:

- A node's absolute drawn rect is computable (walk `parent`→root, accumulate
  `layout.left/top`, subtract each `ScrollNode` ancestor's scroll offset — all
  fields public), and event handlers run against settled current-frame layout,
  so the walk is safe.
- **But** there is no public way for a consumer to obtain a `Node*` for a VNode
  they built. `getHoveredNode()` returns the *deepest* hit node (a Text child
  inside the Row, not the Row). `useRef` stores only scalar fiber state.
  Component fibers have `renderNode == nullptr`. No key→node lookup exists.

So a consumer anchoring a submenu has to reconstruct the row position from
inputs — which is scroll-blind and produced a "follow-the-mouse" bug when the
prefabs cascade used `APP->scene->mousePos.y` as a proxy for the row's drawn Y.

Two capabilities close the gap: **(1) a node ref** (get a `Node*` for an element
you built) and **(2) `absoluteRect(Node*)`** (its drawn rect).

## Decisions (locked)

Names reflect a React-idiom critique pass (3 independent React-dev lenses); see
"React-idiom critique outcome" below. The capability is React-faithful; the
naming was tightened to match burned-in React vocabulary, and one real temporal
trap was fixed.

1. **Node-ref shape: a `useElementRef()` hook returning a `shared_ptr`-backed
   handle.** The consumer attaches it with `.ref(handle)` on a builder. Matches
   yui's hook vocabulary (`useState`/`useRef`/`useEffect`) and React's mental
   model (you ref *elements*); the handle is copyable into closures (the
   `onHover` capture needs this), one heap alloc per ref'd element. Read with
   `.current()` (primary, matches React's `ref.current`); `.get()` is a kept
   alias for C++ smart-pointer muscle memory.
   - **Handle stability (React-critical):** `useElementRef()` returns a handle
     backed by the *same* `shared_ptr` control block every render (the control
     block lives in `hookState`; each render hands out a copy observing the same
     slot). This is the React `useRef`-identity-is-stable guarantee — a consumer
     may capture the handle once in a closure / `useEffect` and trust it across
     renders. Not stating this was a spec defect the critique caught.
2. **Liveness: lazy `current()`-time check only.** The handle holds a
   `weak_ptr<bool>` copy of `Node::alive`; `current()`/`get()` validates before
   returning, else `nullptr`. No eager clearing wired into the reconciler removal
   path — the difference is unobservable (the only reader is `current()`, which
   checks), and it mirrors the existing `liveFocusedInput()` lazy pattern.
3. **Render-phase read returns `nullptr` (React parity, fixes the worst trap).**
   `current()`/`get()` returns `nullptr` when called during the render phase
   (`ComponentContext` knows it is mid-render — it drives the hook indices),
   reading the live node ONLY from event handlers / post-layout. Without this,
   a render-body read would return a *stale, non-null* node (last frame's
   layout) and `getBoundingRect` would yield silently-wrong geometry that passes
   static tests and glitches only under animation/scroll/resize. Returning null
   during render makes the failure loud and **exactly matches React** ("refs are
   null during render"). This is a design guard, not a docs band-aid.
4. **`getBoundingRect`: a method on the ref forwarding to a free core function.**
   The method `NodeRef::getBoundingRect()` is the public face (matches the DOM's
   `element.getBoundingClientRect()` — geometry-of-an-element is the web's one
   canonical *method* idiom). It forwards to a free function `yui::absoluteRect(
   const Node*)` returning `layout::Rect`, which is where the walk actually
   lives — keeping `layout/` pure (it must not depend on `Node`). The free
   function stays public for any raw-`Node*` holder; consumers normally never
   see it. ("absoluteRect" as the public name was rejected: it mis-cues to CSS
   `position: absolute` rather than screen-space.) Returns screen/viewport space
   (document this — a React dev assumes viewport-relative and is right).
5. **Ref scope: host elements only (`.ref()` on `BuilderBase`).** A host element
   has exactly one node; a Component renders a subtree with `renderNode ==
   nullptr`, so a component ref would need an invented convention ("first host
   descendant") — a separate, deliberate feature. `.ref()` lives on
   `BuilderBase` and Component is NOT a `BuilderBase`, so `Component().ref(...)`
   is a **compile error, not a silent no-op** — the honest boundary (React devs
   trained by `forwardRef` will try it; a compile wall teaches the rule
   immediately). Component refs can extend this later without breaking it.

## Why the rejected node-ref shapes lose

- **`.ref(std::function<void(Node*)>)` callback prop** — props are wholesale-
  replaced every render (`updateProps` does `props = newProps`; `std::function`
  isn't comparable), so a ref-callback fires *every frame*, not once on mount.
  To fire once + survive re-renders the consumer needs stable storage anyway —
  i.e. a `useRef`, which is the hook shape with extra steps. Also pushes a user
  callback onto the hot reconcile path.
- **`Host::findNode(key)`** — the keyed-fiber lookup is transient (rebuilt per
  `reconcileChildren`, discarded); no persistent key→node map exists. Keys are
  only sibling-unique, so a global lookup collides, and most nodes have no key.
  Worse ergonomics, still returns a raw `Node*` needing the same guard.

## Liveness mechanism (no new token needed)

`Node::alive` (`std::shared_ptr<bool>`) already exists and is cleared in
`~Node`. The ref mirrors `EventHandler`'s focused-input handling: store
`slot->node` and `slot->alive = node->alive` (a `weak_ptr` copy) in lockstep at
population; `get()` does `lock()` + `*alive` check, returning `nullptr` if dead.
Both dangle modes are caught: unmount/destroy (`~Node` sets `*alive=false`) and
remount-replaces (old node dies; the ref self-heals because `reconcileHost`/
`mountHost` re-populates the slot on the next render). Invalidation is automatic;
no `onNodeRemoved` change required.

## Where things live / blast radius (additive, 5 files)

| File | Change | Public? |
|---|---|---|
| `core/VNode.hpp` | `NodeRef` fwd + optional ref slot on `VNode`; `BuilderBase::ref()` | yes |
| `core/ComponentContext.hpp` | `useElementRef()` decl + impl | yes |
| `core/NodeRef.hpp` (new) | `NodeRef` / `NodeRefSlot` definition | yes |
| `core/Reconciler.cpp` | populate ref slot at `mountHost` / `reconcileHost` | no |
| new decl + `.cpp` | `yui::absoluteRect(const Node*)` | yes |

Per-frame cost: zero for non-ref'd elements (one `if (vnode.ref)` null-check at
node creation). `useElementRef` costs one `hookState` slot, like `useRef`. Purely
additive; no existing signature changes.

## Consumer end-to-end

```cpp
auto rowRef = ctx.useElementRef();                 // stable handle; survives re-render, dies on unmount
return Row({ Text(label), Spacer(), Text("▸") })
    .ref(rowRef)                                   // attaches to the ROW BoxNode, not deep Text
    .onHover([rowRef, /*...*/](bool over) {
        if (!over) return;
        Node* row = rowRef.current();              // nullptr if reconciled away OR during render — safe
        if (!row) return;
        auto r = rowRef.getBoundingRect();         // drawn rect: scroll + clamp baked in (screen space)
        auto p = yui::layout::placeSubmenu(r, r.y, {nextW, contentH}, vp, yui::layout::Side::Right);
        setSubs(/* push SubmenuEntry at {p.x, p.y} */);
    });
```

This yields the **Row** node (not the deep Text), eliminating both the
deepest-node problem and the `mousePos.y` proxy. `r.y` is the row's true drawn
top; `r.x`/`r.w` are the real parent rect `placeSubmenu` wants. Maps 1:1 onto the
React popover idiom: `useRef → ref={} → getBoundingClientRect → floating-ui`
(where `placeSubmenu` is the flip/shift "middleware").

## React-idiom critique outcome

Three independent React-developer lenses reviewed the design before build. Verdict:
the *flow* is React-faithful (1:1 with the `useRef`/`ref=`/`getBoundingClientRect`/
floating-ui popover idiom); the changes were naming + one temporal trap:

- **Renames (adopted):** `useNodeRef`→`useElementRef`, `.get()`→`.current()` (alias
  kept), free `absoluteRect`→ a `getBoundingRect()` method forwarding to the free
  function. Each requested independently by 2+ reviewers; all leak less C++ substrate
  at the spots React has burned-in vocabulary.
- **Worst trap (fixed by decision 3):** a render-phase `get()` returned a stale
  non-null node → silently-wrong geometry. Now returns `nullptr` during render,
  matching React. This was the single highest-value finding.
- **Spec defect (fixed in decision 1):** handle stability across renders was
  unstated; now pinned (same `shared_ptr` control block every render).
- **Confirmed native, unchanged:** `.ref()` attach syntax, populate-at-commit /
  read-in-handlers timing, host-only scoping (as a compile error), the two-hook
  split (`useRef` for values returns `T&`; element refs genuinely can't share that
  shape, so a second hook is justified — not an "ew").
