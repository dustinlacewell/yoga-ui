#pragma once

#include "Clipboard.hpp"
#include "DirtyScheduler.hpp"
#include "EditCommand.hpp"
#include "ErrorHandler.hpp"
#include "EventHandler.hpp"
#include "Fiber.hpp"
#include "Node.hpp"
#include "VNode.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace yui {

// The reconciliation algorithm is an internal (non-installed) header,
// include/yui/detail/Reconciler.hpp. Host owns it through a unique_ptr so the
// 1.0 public surface forward-declares the type rather than dragging the
// internal definition into the frozen ABI. The members that need Reconciler's
// complete type (the ctor, the dtor, and update()) are defined out of line in
// src/core/Host.cpp, which includes the detail header.
class Reconciler;

// Why an update() produced an all-false result. Ok is the steady-state no-op;
// the others are early-returns that a caller would otherwise mistake for one —
// they signal misconfiguration, not "nothing changed".
enum class UpdateStatus {
    Ok,            // ran normally (whether or not anything changed)
    NoRenderFn,    // no render function set (setRender never called)
    EmptyRender,   // render function returned VNode::empty()
    ZeroViewport,  // width <= 0 || height <= 0
    Reentrant,     // called from within an in-flight update() (ignored, see below)
    Deferred,      // called from within event dispatch — coalesced and applied
                   // same-frame after dispatch unwinds (see below)
};

// Result of Host::update() - tells caller what happened
struct UpdateResult {
    bool needsRepaint = false;   // something visual changed, should render
    bool layoutChanged = false;  // layout was recalculated
    bool animating = false;      // animation in progress (smooth scroll, etc)
    UpdateStatus status = UpdateStatus::Ok;  // why the result is as it is
};

class Host;

// --- Linkage contract for the process-global state below ---------------------
//
// liveHosts/liveHostsMutex (the host registry consulted by Store notification)
// and the currentRender* thread-locals are `inline`, so they collapse to a
// single definition within ONE linked module. yui's supported consumption model
// is source-embedded per module: each executable or plugin compiles yui's
// sources into itself (see yui.mk for VCV Rack plugins) and gets its own private
// copy of this state. That is correct and intended — a module only ever
// registers and notifies its own Hosts.
//
// What is NOT supported: sharing yui objects (Host, Store, Fiber, VNode) across
// a module boundary — e.g. a Store created in one DLL/.so whose subscribers live
// in another. Because each module has its own `liveHosts`, cross-module
// notification would silently consult the wrong registry. If yui is ever built
// as a single shared library linked by multiple modules, this state must be
// exported from one TU (YUI_API/dllexport + a single definition) so all modules
// share it. 1.0 ships static/source-embedded and makes no cross-module promise.

// Global registry of live hosts for safe Store notification
namespace detail {
inline std::unordered_set<Host*> liveHosts;
inline std::mutex liveHostsMutex;
}  // namespace detail

// Current render context - set during render() for Store subscriptions
inline thread_local Host* currentRenderHost = nullptr;

// Current fiber being rendered - for component-level Store subscriptions
inline thread_local Fiber* currentRenderFiber = nullptr;

// RAII guard for render context
class RenderContext {
public:
    explicit RenderContext(Host* host) : prev_(currentRenderHost) { currentRenderHost = host; }
    ~RenderContext() { currentRenderHost = prev_; }
    RenderContext(const RenderContext&) = delete;
    RenderContext& operator=(const RenderContext&) = delete;

private:
    Host* prev_;
};

// RAII guard for fiber render context
class FiberRenderContext {
public:
    explicit FiberRenderContext(Fiber* fiber) : prev_(currentRenderFiber) {
        currentRenderFiber = fiber;
    }
    ~FiberRenderContext() { currentRenderFiber = prev_; }
    FiberRenderContext(const FiberRenderContext&) = delete;
    FiberRenderContext& operator=(const FiberRenderContext&) = delete;

private:
    Fiber* prev_;
};

// Base class for yui host implementations.
// Provides core reconciliation, layout, and event handling.
// Subclasses implement platform-specific rendering and event forwarding.
//
// THREADING CONTRACT
// ------------------
// yui is single-threaded. The Host, event dispatch, reconciliation, and
// rendering must all be driven from ONE thread (typically the UI/main thread):
// update(), setRender(), every handle*() entry point, root()/replaceRenderRoot(),
// and the construction/destruction of the VNode tree your render function builds.
// Calling any of these concurrently from another thread is a data race (UB).
//
// The ONLY sanctioned cross-thread operation is Store::set() (see Store.hpp).
// A Store::set() from another thread marks the relevant dirty flags safely:
// the Host flags it touches (dirty_, componentsDirty_) are std::atomic, marked
// with RELEASE and read with ACQUIRE in update() so the state the producer wrote
// first (the store's new value, the fiber dirty flags) is published to the host
// thread. Host liveness is checked-AND-marked together under a mutex (see
// detail::markHostIfLive), closing the window where a host could be freed between
// the check and the mark. set() does NOT itself reconcile or render — it only
// flips the dirty flags. The actual re-render is applied on the host thread at
// the next update(). Concretely: a worker/audio/network thread may call
// Store::set(); the change becomes visible on screen when the host thread next
// calls update().
//
// Host realises DirtyScheduler — the narrow back-channel core uses for dirty
// scheduling, the diagnostic sink, and render-root handoff. Core holds a
// DirtyScheduler* (not a Host*), so the inheritance is what lets setHost(this)
// upcast and keeps core depending only on the interface, breaking the cycle.
class Host : public DirtyScheduler {
public:
    // Defined out of line in Host.cpp: the ctor wires up reconciler_ (complete
    // type only available there) and the dtor must see the complete Reconciler
    // for unique_ptr's deleter.
    Host();
    virtual ~Host();

    // Non-copyable, non-movable
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // Set the render function (VNode factory).
    //
    // DEPTH PRECONDITION: the returned VNode tree must not nest deeper than
    // ~kMaxTreeDepth (1024) levels. Reconciliation (mount/reconcile) recurses one
    // native stack frame per level and is deliberately NOT depth-guarded — aborting
    // mid-reconcile would leave the fiber/render trees partially built and
    // inconsistent, which is worse than a clean failure. The event-path walks
    // (hit-test, dispatch, key routing) ARE guarded and degrade gracefully, so the
    // practical ceiling is set by reconciliation. Realistic UIs nest a few dozen
    // levels deep; a tree exceeding the limit indicates unbounded data-driven
    // nesting that should be flattened or windowed at the source.
    void setRender(std::function<VNode()> renderFn) {
        render_ = std::move(renderFn);
        markDirty();
    }

    // Set the render function (Component) - auto-wraps in a full-size container
    void setRender(Component comp) {
        render_ = [comp = std::move(comp)]() -> VNode {
            return Box(std::vector<Child>{comp}).flexGrow(1);
        };
        markDirty();
    }

    // Install the backend text measurer for this host. Stored in the host's
    // yoga config context; TextNode::measureFunc recovers it per measurement.
    // Pass nullptr to clear (measurement falls back to the heuristic).
    //
    // Lifetime is self-managed in both directions — neither object need outlive
    // the other. The measurer self-detaches: ~ITextMeasurer clears this host's
    // context if the measurer dies first, and ~Host deregisters from the
    // measurer if the host dies first. Either destruction order is safe; a dead
    // measurer can never be read from a relayout.
    void setTextMeasurer(ITextMeasurer* measurer) {
        if (measurer == installedMeasurer_)
            return;
        // Drop the prior measurer's back-reference to us before installing the
        // new one, so it does not later clear a context we have repurposed.
        if (installedMeasurer_)
            installedMeasurer_->detachHost(this);
        installedMeasurer_ = measurer;
        YGConfigSetContext(config_.get(), measurer);
        // Every Text node's cached Yoga measurement was produced by the PREVIOUS
        // measurer (or, on first install, by the heuristic fallback used while no
        // measurer was set). Those cached sizes are now wrong — but a measure-func
        // node is only re-measured when it is marked dirty, and reconciliation
        // dirties a Text node only when its string/fontSize prop changes. A node
        // that never changes (e.g. a constant glyph like a submenu chevron) would
        // otherwise keep its stale first-pass fallback width forever, leaving the
        // glyph mis-sized and mis-placed. Swapping the measurer invalidates ALL
        // text measurements, so dirty every measure node in the live tree to force
        // re-measure on the next layout pass — and flag the host dirty so that
        // pass actually runs (update() only calls calculateLayout when dirty_).
        if (renderRoot_) {
            markMeasureNodesDirty(renderRoot_.get());
            markDirty();
        }
        if (measurer) {
            YGConfigRef cfg = config_.get();
            // Run by ~ITextMeasurer (measurer-dies-first) while this host is still
            // alive (gated by the liveness token in ~ITextMeasurer). Sever BOTH
            // directions: null the yoga context AND our back-reference, so ~Host's
            // detachHost guard sees null and never dereferences the freed measurer.
            measurer->attachHost(this, alive_, [this, cfg] {
                YGConfigSetContext(cfg, nullptr);
                installedMeasurer_ = nullptr;
            });
        }
    }

    // Install the platform clipboard for this host. Handed to the event handler
    // per-call for Cut/Copy/Paste, never stored below Host. Pass nullptr to
    // clear (Cut/Copy/Paste then report unconsumed). Unlike setTextMeasurer, a
    // clipboard swap has no layout impact, so nothing is dirtied.
    //
    // Lifetime is self-managed in both directions, mirroring setTextMeasurer:
    // ~IClipboard nulls clipboard_ if the clipboard dies first, and ~Host
    // deregisters from the clipboard if the host dies first.
    void setClipboard(IClipboard* clipboard) {
        if (clipboard == clipboard_)
            return;
        // Drop the prior clipboard's back-reference to us before installing the
        // new one, so it does not later null a pointer we have repointed.
        if (clipboard_)
            clipboard_->detachHost(this);
        clipboard_ = clipboard;
        if (clipboard) {
            // Run by ~IClipboard (clipboard-dies-first) while this host is
            // still alive (gated by the liveness token in ~IClipboard). Severs
            // the back-reference, so ~Host's detach guard sees null and never
            // dereferences the freed clipboard.
            clipboard->attachHost(this, alive_, [this] { clipboard_ = nullptr; });
        }
    }

    // Install the diagnostic sink for exceptions escaping user callbacks. A single
    // host-level sink covers reconciliation, events, and effects. Pass {} to fall
    // back to the default policy (debug-stderr / release-swallow). See reportError.
    void setErrorHandler(ErrorHandler handler) { errorHandler_ = std::move(handler); }

    // Route a caught user-callback exception to the installed sink, or to the
    // default policy when none is installed: in debug, mirror the hook-count
    // diagnostic to stderr; in release, swallow. Marked noexcept — it runs from
    // contexts that are already recovering from (or unwinding) a failed callback,
    // including destructors, and must never add a second exception.
    void reportError(std::string_view where, const std::exception* eOrNull) noexcept override {
        if (errorHandler_) {
            try {
                errorHandler_(where, eOrNull);
            } catch (...) {
                // A throwing sink must not defeat the guarantee it serves.
            }
            return;
        }
#ifndef NDEBUG
        fprintf(stderr, "yui: exception escaped user callback at '%.*s': %s\n",
                static_cast<int>(where.size()), where.data(),
                eOrNull ? eOrNull->what() : "<non-std::exception>");
#endif
    }

    // Mark for re-render on next update. Sanctioned to run cross-thread (from
    // Store::set on a worker thread). RELEASE store: it publishes everything the
    // producer wrote before it (the store's new value_, the fiber dirty flags) to
    // the host thread, which pairs its update() load with an ACQUIRE.
    void markDirty() override { dirty_.store(true, std::memory_order_release); }

    // Mark that at least one component needs re-rendering. Also reachable
    // cross-thread via Store::set -> Fiber::markDirty -> here. RELEASE store: it is
    // the publish point for the fiber.dirty flags written just before it, which the
    // reconciler then reads with relaxed loads ordered by update()'s ACQUIRE load.
    void markComponentDirty() override { componentsDirty_.store(true, std::memory_order_release); }

    bool isDirty() const { return dirty_.load(std::memory_order_acquire); }
    bool needsUpdate() const {
        return dirty_.load(std::memory_order_acquire) ||
               componentsDirty_.load(std::memory_order_acquire);
    }

    // Call each frame to update animations, reconcile if dirty, and layout.
    //
    // noexcept is the edge guarantee: yui runs in-process alongside other plugins,
    // so a throw must never escape into the host (an uncaught throw there
    // terminates the user's whole DAW). The body catches normal throws and routes
    // them to the sink; the noexcept is the backstop for the unexpected (a yui
    // bug) — a throw through it is a clean terminate at the yui edge rather than
    // silent corruption of host state.
    //
    // NOT reentrant: an update() walks the fiber/render trees holding raw pointers
    // into them, so a nested update() would mutate the very trees the outer walk is
    // reading — use-after-free / corruption. A call from within an in-flight
    // update() (e.g. an effect during drainCommit) is therefore ignored and
    // diagnosed (UpdateStatus::Reentrant), and the in-flight update completes
    // normally.
    //
    // A call from within EVENT DISPATCH (a user onClick/onChange/onKeyDown that
    // calls host.update()) is likewise unsafe to run inline — the event walk holds
    // raw pointers into the trees a reconcile would rebuild. Rather than drop it,
    // such a call is DEFERRED (UpdateStatus::Deferred): coalesced into one pending
    // update and applied SAME-FRAME once the top-level handle*() unwinds, before
    // returning to the platform loop (React-style batching). So a handler may call
    // update() freely; the reconcile just happens at the tail of the current event
    // rather than synchronously mid-callback.
    //
    // Defined out of line in Host.cpp: the reconcile/mount calls in the body
    // need Reconciler's complete type, which is the internal detail header.
    UpdateResult update(float width, float height, float dt = 1.f / 60.f) noexcept;

    // Access render root for rendering
    Node* root() const { return renderRoot_.get(); }

    // Adopt a new render root. Called by the reconciler when the root primitive
    // type changes between frames and the old root is torn down and rebuilt
    // (see Reconciler::remountRoot). The host owns renderRoot_ after the initial
    // takeRenderRoot(), so it must swap in the freshly-built root here.
    void replaceRenderRoot(std::unique_ptr<Node> newRoot) override { renderRoot_ = std::move(newRoot); }

    // Event handling - returns true if event was consumed.
    //
    // Every entrypoint is noexcept: it is a yui edge called directly from the
    // platform's event loop, so a throw must never escape (see Host::update). The
    // EventHandler already isolates each user callback and routes it to the sink;
    // these guards are the truthful backstop for anything else, so the noexcept
    // never fires for a normal callback throw.
    // `mods` is the KeyMod bitmask held at the press (matches handleKeyDown's
    // keyMod): shims pass the platform modifier state so shift+click consumers
    // can read it from the dispatched event.
    bool handleMouseDown(float x, float y, MouseButton btn, uint16_t mods = KeyMod_None) noexcept {
        return guardedBool("Host::handleMouseDown", [&] {
            if (!renderRoot_)
                return false;
            return eventHandler_.handleMouseDown(renderRoot_.get(), x, y, btn, mods);
        });
    }

    bool handleMouseUp(float x, float y, MouseButton btn) noexcept {
        return guardedBool("Host::handleMouseUp", [&] {
            if (!renderRoot_)
                return false;
            return eventHandler_.handleMouseUp(renderRoot_.get(), x, y, btn);
        });
    }

    void handleMouseMove(float x, float y) noexcept {
        guardedVoid("Host::handleMouseMove", [&] {
            if (!renderRoot_)
                return;
            eventHandler_.handleMouseMove(renderRoot_.get(), x, y);
        });
    }

    bool handleScroll(float x, float y, float dx, float dy) noexcept {
        return guardedBool("Host::handleScroll", [&] {
            if (!renderRoot_)
                return false;
            bool consumed = eventHandler_.handleScroll(renderRoot_.get(), x, y, dx, dy);
            if (consumed)
                markDirty();
            return consumed;
        });
    }

    void handleMouseLeave() noexcept {
        guardedVoid("Host::handleMouseLeave", [&] {
            if (!renderRoot_)
                return;
            // During a captured drag the pointer legitimately leaves the window;
            // synthesizing an off-window move here would inject a spurious
            // captured move. Capture ends at release, which resyncs hover itself.
            if (eventHandler_.hasCapture())
                return;
            eventHandler_.handleMouseMove(renderRoot_.get(), -1, -1);
        });
    }

    bool handleKeyDown(int keyCode, uint16_t keyMod, bool repeat = false) noexcept {
        return guardedBool("Host::handleKeyDown", [&] {
            if (!renderRoot_)
                return false;
            bool consumed = eventHandler_.handleKeyDown(renderRoot_.get(), keyCode, keyMod, repeat);
            if (consumed)
                markDirty();
            return consumed;
        });
    }

    bool handleKeyUp(int keyCode, uint16_t keyMod) noexcept {
        return guardedBool("Host::handleKeyUp", [&] {
            if (!renderRoot_)
                return false;
            bool consumed = eventHandler_.handleKeyUp(renderRoot_.get(), keyCode, keyMod);
            if (consumed)
                markDirty();
            return consumed;
        });
    }

    void handleTextInput(const std::string& text) noexcept {
        guardedVoid("Host::handleTextInput", [&] { eventHandler_.handleTextInput(text); });
    }

    // Route an editing command (see EditCommand.hpp for which commands are live
    // in which commit) to the focused Input. Returns true iff consumed, so a
    // shim can fall through when nothing is focused. `extend` is the Shift-held
    // selection modifier: Move* commands move only the caret, leaving the
    // anchor to span the selection. Cut/Copy/Paste read/write the installed
    // clipboard (see setClipboard); with none installed they report unconsumed.
    bool handleEditCommand(EditCommand cmd, bool extend = false) noexcept {
        return guardedBool("Host::handleEditCommand",
                           [&] { return eventHandler_.handleEditCommand(cmd, extend, clipboard_); });
    }

    void handleSubmit() noexcept {
        guardedVoid("Host::handleSubmit", [&] { eventHandler_.handleSubmit(); });
    }

    InputNode* getFocusedInput() const { return eventHandler_.getFocusedInput(); }
    Node* getFocusedNode() const { return eventHandler_.getFocusedNode(); }

    // Programmatic focus (typically host.focus(ref.current())). Accepts ANY
    // node — .focusable() gates only click/Tab acquisition, not programmatic
    // focus (tabindex=-1 parity). Wrapped in the dispatch guard: an onFocus
    // that calls update() defers and drains same-frame (see guardedVoid).
    void focus(Node* node) noexcept {
        guardedVoid("Host::focus", [&] { eventHandler_.focusNode(node); });
    }

    void blur() noexcept {
        guardedVoid("Host::blur", [&] { eventHandler_.focusNode(nullptr); });
    }

    // Move focus through the focusables in document order, wrapping — scoped to
    // the focus trap while one is set. Tab detection lives in the platform shim
    // (core stays keycode-agnostic: SDL's Tab is 9, GLFW's is 258): call these
    // on Tab / Shift-Tab that handleKeyDown did not report consumed.
    void focusNext() noexcept {
        guardedVoid("Host::focusNext", [&] {
            if (renderRoot_)
                eventHandler_.focusNext(renderRoot_.get());
        });
    }

    void focusPrev() noexcept {
        guardedVoid("Host::focusPrev", [&] {
            if (renderRoot_)
                eventHandler_.focusPrev(renderRoot_.get());
        });
    }

    // Scope Tab traversal to `node`'s subtree (e.g. an open modal). A trap root
    // removed by a later reconcile silently unscopes (liveness token).
    void setFocusTrap(Node* node) noexcept {
        guardedVoid("Host::setFocusTrap", [&] { eventHandler_.setFocusTrap(node); });
    }

    void clearFocusTrap() noexcept {
        guardedVoid("Host::clearFocusTrap", [&] { eventHandler_.clearFocusTrap(); });
    }

    // The mouse cursor the platform should show right now: the captured node's
    // chain wins during a drag, else the hovered chain; the first explicit
    // .cursor() prop wins, an Input defaults to IBeam, fallback Arrow. Pull
    // query — poll it after update() and map to the native cursor.
    CursorShape getCursor() const { return eventHandler_.getCursor(); }

    bool hasClickHandler(float x, float y, MouseButton btn) {
        if (!renderRoot_)
            return false;
        return eventHandler_.hasClickHandler(renderRoot_.get(), x, y, btn);
    }

private:
    // Recursively dirty every measure-func node (Text) in a subtree so the next
    // layout pass re-measures it. Used when the text measurer is swapped: all
    // cached measurements were produced by the old measurer / fallback and are
    // stale. Only Text nodes carry a Yoga measure func, and YGNodeMarkDirty asserts
    // a measure func exists — so guard on the node type.
    static void markMeasureNodesDirty(Node* node) {
        if (!node)
            return;
        if (node->type() == PrimitiveType::Text && node->yogaNode)
            YGNodeMarkDirty(node->yogaNode);
        for (auto& child : node->children)
            markMeasureNodesDirty(child.get());
    }

    // RAII latch marking a live event dispatch, so a user callback that calls
    // update() defers rather than reconciling into the trees the event walk is
    // reading. Saves/restores the prior value so nested handle*() calls compose.
    struct DispatchGuard {
        bool& flag;
        bool prev;
        explicit DispatchGuard(bool& f) : flag(f), prev(f) { flag = true; }
        ~DispatchGuard() { flag = prev; }
    };

    // Apply a deferred update() coalesced during dispatch. Runs the reconcile
    // SAME-FRAME — after the outermost handle*() has unwound inDispatch_ but before
    // returning to the platform loop — so a state change a handler made is reflected
    // this frame. No-op while still dispatching (only the outermost drain fires) or
    // when nothing was deferred. noexcept: called from noexcept entrypoints.
    void drainPendingUpdate() noexcept {
        if (inDispatch_ || !pendingUpdate_)
            return;
        pendingUpdate_ = false;
        // Invariant: lastWidth_/lastHeight_ were set by a prior update() call. A
        // handler can only fire after the first mount, and the first mount goes
        // through update() (which records the dimensions), so a deferred update
        // never reaches here with unset dimensions.
        //
        // The drained update has no platform caller to observe its result, so the
        // dirt it consumed (reconcile repaint, relayout, the event handler's
        // visual-state latch) must not vanish here: OR-fold it into carriedResult_,
        // which the next explicit update() reports and clears. OR-accumulate
        // because several dispatches may drain before that update; a drain that
        // early-returned folds all-false (a no-op).
        UpdateResult drained = update(lastWidth_, lastHeight_);
        carriedResult_.needsRepaint |= drained.needsRepaint;
        carriedResult_.layoutChanged |= drained.layoutChanged;
        carriedResult_.animating |= drained.animating;
    }

    // Backstop wrappers for the noexcept event entrypoints: run the body under the
    // dispatch latch, and on a throw that slipped past the per-callback isolation
    // inside EventHandler route it to the sink instead of propagating through
    // noexcept. After the outermost dispatch unwinds, apply any deferred update.
    // bool variants return false (event not consumed) on failure.
    template <typename Fn>
    bool guardedBool(std::string_view where, Fn&& fn) noexcept {
        bool result = false;
        {
            DispatchGuard dispatchGuard(inDispatch_);
            try {
                result = fn();
            } catch (const std::exception& e) {
                reportError(where, &e);
            } catch (...) {
                reportError(where, nullptr);
            }
        }
        drainPendingUpdate();
        return result;
    }

    template <typename Fn>
    void guardedVoid(std::string_view where, Fn&& fn) noexcept {
        {
            DispatchGuard dispatchGuard(inDispatch_);
            try {
                fn();
            } catch (const std::exception& e) {
                reportError(where, &e);
            } catch (...) {
                reportError(where, nullptr);
            }
        }
        drainPendingUpdate();
    }

    // Tag an early-return UpdateResult with its status and emit ONE diagnostic
    // through the existing error sink — but only on a state-transition, so a
    // caller stuck in a misconfigured state (e.g. a headless host with no render
    // fn) gets a single report per entry into that state, not one per frame. The
    // sink is severity-free (where + optional exception); these are non-fatal
    // diagnostics, so eOrNull is null and the label carries the message.
    UpdateResult earlyReturn(UpdateResult result, UpdateStatus status,
                             std::string_view message) noexcept {
        result.status = status;
        if (status != lastReportedStatus_) {
            lastReportedStatus_ = status;
            reportError(message, nullptr);
        }
        return result;
    }

protected:
    // Per-host yoga config. Render nodes are created against it so their measure
    // callback can recover this host's text measurer from the config context.
    // Declared first so it is destroyed LAST — after the node trees that hold
    // yoga nodes referencing it, so the config always outlives those nodes.
    struct ConfigDeleter {
        void operator()(YGConfigRef c) const { YGConfigFree(c); }
    };
    std::unique_ptr<std::remove_pointer_t<YGConfigRef>, ConfigDeleter> config_{YGConfigNew()};

    std::function<VNode()> render_;
    ErrorHandler errorHandler_;
    // Owned through a pointer so the public header only forward-declares
    // Reconciler (the algorithm lives in the non-installed detail header).
    // Constructed in Host's ctor; the out-of-line dtor sees the complete type.
    std::unique_ptr<Reconciler> reconciler_;
    EventHandler eventHandler_;

    // Two trees
    std::unique_ptr<Fiber> fiberRoot_;
    std::unique_ptr<Node> renderRoot_;

    float lastWidth_ = 0;
    float lastHeight_ = 0;

    // Dirty flags. These are the ONE piece of Host state written cross-thread:
    // Store::set() (the sole sanctioned off-thread entry, see the class-level
    // threading contract) flips them via markDirty()/markComponentDirty(). They
    // are atomic so the off-thread write and the host-thread read in update() are
    // not a data race — AND they are the publish/consume handoff for the state the
    // producer wrote first: markDirty()/markComponentDirty() store with RELEASE and
    // update()/needsUpdate()/isDirty() load with ACQUIRE. That pairing is what makes
    // the fiber.dirty flags (written before markComponentDirty, then read RELAXED in
    // the reconciler) and the store's new value_ visible to the host thread once it
    // observes the flag set. Relaxed alone would permit a lost wakeup / stale read.
    std::atomic<bool> dirty_{true};
    std::atomic<bool> componentsDirty_{false};

    // Reentrancy latch for update(). True while an update() is in flight; a nested
    // call observes it and bails (see update()). Managed by an RAII guard so it is
    // cleared on every exit path, including the render-callback rethrow.
    bool inUpdate_ = false;

    // Dispatch latch, DISTINCT from inUpdate_. True while a top-level handle*()
    // event entrypoint is running (set by DispatchGuard in guardedBool/guardedVoid).
    // A user callback that calls update() while this is set defers instead of
    // reconciling into the live trees. inDispatch_ is set ONLY in the event
    // entrypoints; inUpdate_ is set ONLY in update() — they never overlap.
    bool inDispatch_ = false;
    // Set when an update() was deferred during dispatch; drained same-frame once
    // dispatch fully unwinds.
    bool pendingUpdate_ = false;
    // Result bits a drained (deferred) update reported with no platform caller to
    // see them (see drainPendingUpdate). Folded into the next normally-completing
    // update() and cleared there, so an internal drain never makes dirt vanish.
    // Only the three signal bits carry; status is unused.
    UpdateResult carriedResult_;

    // Last early-return status reported through the sink. update() only emits a
    // diagnostic when the status changes, so a persistent misconfiguration is
    // reported once per transition into it rather than every frame. Reset to Ok
    // on any normally-completing update so re-entering a bad state re-fires.
    UpdateStatus lastReportedStatus_ = UpdateStatus::Ok;

    // Currently installed text measurer (raw, non-owning). Tracked so the host
    // can deregister from it on replacement and in ~Host.
    ITextMeasurer* installedMeasurer_ = nullptr;

    // Currently installed platform clipboard (raw, non-owning). Same tracking
    // discipline as installedMeasurer_: deregistered on replacement and in
    // ~Host; handed to the event handler per-call, never stored below here.
    IClipboard* clipboard_ = nullptr;

    // Liveness token shared with the measurer's and clipboard's registration
    // records, mirroring the Fiber/Store alive_ idiom. Cleared in ~Host so a
    // measurer/clipboard destroyed afterwards observes the host as dead and
    // skips clearing our freed state.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

namespace detail {
// Mark a host dirty IF it is still live — the check and the mark happen together
// under liveHostsMutex, closing the TOCTOU window a separate isHostLive()+markDirty
// left open (the host could be destroyed between the two). ~Host erases itself from
// liveHosts under liveHostsMutex BEFORE tearing down members, so a host we still
// observe in the registry while holding the lock cannot be freed under markDirty().
// markDirty() only flips an atomic flag, so holding liveHostsMutex across it is cheap.
inline void markHostIfLive(Host* host) {
    std::lock_guard lock(liveHostsMutex);
    if (liveHosts.count(host) > 0) {
        host->markDirty();
    }
}
}  // namespace detail

}  // namespace yui
