#pragma once

#include "ErrorHandler.hpp"
#include "EventHandler.hpp"
#include "Fiber.hpp"
#include "Node.hpp"
#include "Reconciler.hpp"
#include "VNode.hpp"

#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_set>

namespace yui {

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
class Host {
public:
    Host() {
        std::lock_guard lock(detail::liveHostsMutex);
        detail::liveHosts.insert(this);

        reconciler_.setNodeRemovedCallback([this](Node* node) { eventHandler_.onNodeRemoved(node); });
        reconciler_.setAutoFocusCallback([this](InputNode* node) { eventHandler_.focusInput(node); });
        reconciler_.setHost(this);
        reconciler_.setConfig(config_.get());

        // The event handler routes a throwing user callback to the same sink the
        // reconciler uses. The lambda forwards through reportError so the default
        // policy (debug-stderr / release-swallow) lives in one place.
        eventHandler_.setErrorHandler(
            [this](std::string_view where, const std::exception* e) { reportError(where, e); });
    }

    virtual ~Host() {
        {
            std::lock_guard lock(detail::liveHostsMutex);
            detail::liveHosts.erase(this);
        }
        // host-dies-first: deregister from the measurer so its destructor never
        // touches our (about-to-be-freed) config. Marking alive_ false is a
        // belt-and-braces guard for any registration that lingers.
        if (installedMeasurer_)
            installedMeasurer_->detachHost(this);
        installedMeasurer_ = nullptr;
        *alive_ = false;
        if (fiberRoot_) {
            fiberRoot_->willUnmount();
        }
        // Drop the measurer reference before the node tree is torn down so no
        // node can reach a dangling measurer mid-destruction. config_ itself is
        // freed last (it is declared before the trees, so destroyed after them).
        YGConfigSetContext(config_.get(), nullptr);
    }

    // Non-copyable, non-movable
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // Set the render function (VNode factory)
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
            measurer->attachHost(this, alive_, [cfg] { YGConfigSetContext(cfg, nullptr); });
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
    void reportError(std::string_view where, const std::exception* eOrNull) noexcept {
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

    // Mark for re-render on next update
    void markDirty() { dirty_ = true; }

    // Mark that at least one component needs re-rendering
    void markComponentDirty() { componentsDirty_ = true; }

    bool isDirty() const { return dirty_; }
    bool needsUpdate() const { return dirty_ || componentsDirty_; }

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
    UpdateResult update(float width, float height, float dt = 1.f / 60.f) noexcept {
        UpdateResult result;
        if (inUpdate_)
            return earlyReturn(result, UpdateStatus::Reentrant,
                               "Host::update: reentrant call ignored");
        if (!render_)
            return earlyReturn(result, UpdateStatus::NoRenderFn,
                               "Host::update: no render function set");
        if (width <= 0 || height <= 0)
            return earlyReturn(result, UpdateStatus::ZeroViewport,
                               "Host::update: viewport has non-positive dimensions");

        // RAII reentrancy latch: set on entry past the guard, cleared on every
        // exit path — early-returns below, the render-callback rethrow, and any
        // unexpected throw caught by the outer try. A manual reset before each
        // return would be fragile across those paths.
        struct InUpdateGuard {
            bool& flag;
            explicit InUpdateGuard(bool& f) : flag(f) { flag = true; }
            ~InUpdateGuard() { flag = false; }
        } inUpdateGuard(inUpdate_);

        try {
            // Update animations every frame
            if (renderRoot_) {
                result.animating = renderRoot_->update(dt);
            }

            // Check if size changed
            if (width != lastWidth_ || height != lastHeight_) {
                lastWidth_ = width;
                lastHeight_ = height;
                dirty_ = true;
            }

            // Process dirty components - walk FIBER tree
            bool componentsReconciled = false;
            if (componentsDirty_ && fiberRoot_) {
                componentsDirty_ = false;
                componentsReconciled = reconciler_.reconcileDirtyComponents(fiberRoot_.get());
            }

            // Full re-render only for structural changes
            bool fullReconcile = false;
            if (dirty_) {
                dirty_ = false;
                fullReconcile = true;

                VNode vnode;
                {
                    // The render fn is a user callback; isolate a throw so the
                    // frame degrades to "no structural change" rather than escaping.
                    RenderContext ctx(this);
                    try {
                        vnode = render_();
                    } catch (const std::exception& e) {
                        reportError("render", &e);
                        return result;
                    } catch (...) {
                        reportError("render", nullptr);
                        return result;
                    }
                }
                if (vnode.isEmpty)
                    return earlyReturn(result, UpdateStatus::EmptyRender,
                                       "Host::update: root rendered empty");

                if (!fiberRoot_) {
                    fiberRoot_ = reconciler_.mount(vnode);
                    renderRoot_ = reconciler_.takeRenderRoot();
                } else {
                    reconciler_.reconcile(fiberRoot_.get(), vnode);
                }
            }

            // Re-layout on RENDER tree
            if (renderRoot_ && (fullReconcile || componentsReconciled)) {
                renderRoot_->calculateLayout(width, height);
                result.layoutChanged = true;
            }

            // Set needsRepaint if anything visual changed
            result.needsRepaint = fullReconcile || componentsReconciled || result.animating;
        } catch (const std::exception& e) {
            reportError("Host::update", &e);
        } catch (...) {
            reportError("Host::update", nullptr);
        }

        // Reached the steady-state path: clear the latch so a later transition
        // back into a misconfigured state re-emits its diagnostic.
        lastReportedStatus_ = UpdateStatus::Ok;
        return result;
    }

    // Access render root for rendering
    Node* root() const { return renderRoot_.get(); }

    // Adopt a new render root. Called by the reconciler when the root primitive
    // type changes between frames and the old root is torn down and rebuilt
    // (see Reconciler::remountRoot). The host owns renderRoot_ after the initial
    // takeRenderRoot(), so it must swap in the freshly-built root here.
    void replaceRenderRoot(std::unique_ptr<Node> newRoot) { renderRoot_ = std::move(newRoot); }

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
                dirty_ = true;
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
                dirty_ = true;
            return consumed;
        });
    }

    bool handleKeyUp(int keyCode, uint16_t keyMod) noexcept {
        return guardedBool("Host::handleKeyUp", [&] {
            if (!renderRoot_)
                return false;
            bool consumed = eventHandler_.handleKeyUp(renderRoot_.get(), keyCode, keyMod);
            if (consumed)
                dirty_ = true;
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
    Reconciler reconciler_;
    EventHandler eventHandler_;

    // Two trees
    std::unique_ptr<Fiber> fiberRoot_;
    std::unique_ptr<Node> renderRoot_;

    float lastWidth_ = 0;
    float lastHeight_ = 0;
    bool dirty_ = true;
    bool componentsDirty_ = false;

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
