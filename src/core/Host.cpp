#include <yui/core/Host.hpp>

#include <yui/detail/Reconciler.hpp>

namespace yui {

// The ctor, dtor, and update() are defined here rather than in Host.hpp because
// each needs Reconciler's complete type, and Reconciler lives in the internal
// (non-installed) detail header. Host.hpp only forward-declares Reconciler so the
// 1.0 public surface never drags the reconciliation algorithm into the frozen ABI.

Host::Host() : reconciler_(std::make_unique<Reconciler>()) {
    std::lock_guard lock(detail::liveHostsMutex);
    detail::liveHosts.insert(this);

    reconciler_->setNodeRemovedCallback([this](Node* node) { eventHandler_.onNodeRemoved(node); });
    reconciler_->setAutoFocusCallback([this](InputNode* node) { eventHandler_.focusInput(node); });
    reconciler_->setHost(this);
    reconciler_->setConfig(config_.get());

    // The event handler routes a throwing user callback to the same sink the
    // reconciler uses. The lambda forwards through reportError so the default
    // policy (debug-stderr / release-swallow) lives in one place.
    eventHandler_.setErrorHandler(
        [this](std::string_view where, const std::exception* e) { reportError(where, e); });
}

Host::~Host() {
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

UpdateResult Host::update(float width, float height, float dt) noexcept {
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
            dirty_.store(true, std::memory_order_relaxed);
        }

        // Process dirty components - walk FIBER tree.
        // ACQUIRE load pairs with markComponentDirty()'s RELEASE store: once we
        // observe componentsDirty_ set, the fiber.dirty flags the producer wrote
        // before it are visible, so the reconciler's relaxed reads of them are
        // correctly ordered. The clear store stays relaxed (host-thread only).
        bool componentsReconciled = false;
        if (componentsDirty_.load(std::memory_order_acquire) && fiberRoot_) {
            componentsDirty_.store(false, std::memory_order_relaxed);
            componentsReconciled = reconciler_->reconcileDirtyComponents(fiberRoot_.get());
        }

        // Full re-render only for structural changes.
        // ACQUIRE load pairs with markDirty()'s RELEASE store (publishes the store's
        // new value_ before the render reads it). The clear store stays relaxed.
        bool fullReconcile = false;
        if (dirty_.load(std::memory_order_acquire)) {
            dirty_.store(false, std::memory_order_relaxed);
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
                fiberRoot_ = reconciler_->mount(vnode);
                renderRoot_ = reconciler_->takeRenderRoot();
            } else {
                reconciler_->reconcile(fiberRoot_.get(), vnode);
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

}  // namespace yui
