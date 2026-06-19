#pragma once

#include "DirtyScheduler.hpp"
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
// the Host flags it touches (dirty_, componentsDirty_) are std::atomic, and
// host liveness is checked under a mutex (see detail::liveHosts / isHostLive).
// set() does NOT itself reconcile or render — it only flips the dirty flags.
// The actual re-render is applied on the host thread at the next update().
// Concretely: a worker/audio/network thread may call Store::set(); the change
// becomes visible on screen when the host thread next calls update().
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
    // Store::set on a worker thread); the atomic store is the synchronisation.
    void markDirty() override { dirty_.store(true, std::memory_order_relaxed); }

    // Mark that at least one component needs re-rendering. Also reachable
    // cross-thread via Store::set -> Fiber::markDirty -> here.
    void markComponentDirty() override { componentsDirty_.store(true, std::memory_order_relaxed); }

    bool isDirty() const { return dirty_.load(std::memory_order_relaxed); }
    bool needsUpdate() const {
        return dirty_.load(std::memory_order_relaxed) ||
               componentsDirty_.load(std::memory_order_relaxed);
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
    // into them, so a nested update() (e.g. a user onClick/onChange/effect that
    // calls host.update()) would mutate the very trees the outer walk is reading —
    // use-after-free / corruption. A reentrant call is therefore ignored and
    // diagnosed (UpdateStatus::Reentrant), and the in-flight update completes
    // normally. Do not call update() from within a yui event/effect callback;
    // mark the host dirty instead and let the next frame's update() pick it up.
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
    bool handleMouseDown(float x, float y, MouseButton btn) noexcept {
        return guardedBool("Host::handleMouseDown", [&] {
            if (!renderRoot_)
                return false;
            return eventHandler_.handleMouseDown(renderRoot_.get(), x, y, btn);
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

    void handleBackspace() noexcept {
        guardedVoid("Host::handleBackspace", [&] { eventHandler_.handleBackspace(); });
    }

    void handleSubmit() noexcept {
        guardedVoid("Host::handleSubmit", [&] { eventHandler_.handleSubmit(); });
    }

    InputNode* getFocusedInput() const { return eventHandler_.getFocusedInput(); }

    bool hasClickHandler(float x, float y, MouseButton btn) {
        if (!renderRoot_)
            return false;
        return eventHandler_.hasClickHandler(renderRoot_.get(), x, y, btn);
    }

private:
    // Backstop wrappers for the noexcept event entrypoints: run the body, and on a
    // throw that slipped past the per-callback isolation inside EventHandler route
    // it to the sink instead of propagating through noexcept. bool variants return
    // false (event not consumed) on failure.
    template <typename Fn>
    bool guardedBool(std::string_view where, Fn&& fn) noexcept {
        try {
            return fn();
        } catch (const std::exception& e) {
            reportError(where, &e);
        } catch (...) {
            reportError(where, nullptr);
        }
        return false;
    }

    template <typename Fn>
    void guardedVoid(std::string_view where, Fn&& fn) noexcept {
        try {
            fn();
        } catch (const std::exception& e) {
            reportError(where, &e);
        } catch (...) {
            reportError(where, nullptr);
        }
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
    // are atomic so that off-thread write and the host-thread read in update()
    // are not a data race. They are independent flags, not a lock, so relaxed
    // ordering suffices — set() only needs the flag to eventually be observed by
    // the host thread, which it is at the next update(); no other state is
    // published through them.
    std::atomic<bool> dirty_{true};
    std::atomic<bool> componentsDirty_{false};

    // Reentrancy latch for update(). True while an update() is in flight; a nested
    // call observes it and bails (see update()). Managed by an RAII guard so it is
    // cleared on every exit path, including the render-callback rethrow.
    bool inUpdate_ = false;

    // Last early-return status reported through the sink. update() only emits a
    // diagnostic when the status changes, so a persistent misconfiguration is
    // reported once per transition into it rather than every frame. Reset to Ok
    // on any normally-completing update so re-entering a bad state re-fires.
    UpdateStatus lastReportedStatus_ = UpdateStatus::Ok;

    // Currently installed text measurer (raw, non-owning). Tracked so the host
    // can deregister from it on replacement and in ~Host.
    ITextMeasurer* installedMeasurer_ = nullptr;

    // Liveness token shared with the measurer's registration records, mirroring
    // the Fiber/Store alive_ idiom. Cleared in ~Host so a measurer destroyed
    // afterwards observes the host as dead and skips clearing our freed config.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// Check if a host is still alive (for Store notification)
inline bool isHostLive(Host* host) {
    std::lock_guard lock(detail::liveHostsMutex);
    return detail::liveHosts.count(host) > 0;
}

}  // namespace yui
