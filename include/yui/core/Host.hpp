#pragma once

#include "EventHandler.hpp"
#include "Fiber.hpp"
#include "Node.hpp"
#include "Reconciler.hpp"
#include "VNode.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace yui {

// Result of Host::update() - tells caller what happened
struct UpdateResult {
    bool needsRepaint = false;   // something visual changed, should render
    bool layoutChanged = false;  // layout was recalculated
    bool animating = false;      // animation in progress (smooth scroll, etc)
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
    }

    virtual ~Host() {
        {
            std::lock_guard lock(detail::liveHostsMutex);
            detail::liveHosts.erase(this);
        }
        if (fiberRoot_) {
            fiberRoot_->willUnmount();
        }
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

    // Mark for re-render on next update
    void markDirty() { dirty_ = true; }

    // Mark that at least one component needs re-rendering
    void markComponentDirty() { componentsDirty_ = true; }

    bool isDirty() const { return dirty_; }
    bool needsUpdate() const { return dirty_ || componentsDirty_; }

    // Call each frame to update animations, reconcile if dirty, and layout
    UpdateResult update(float width, float height, float dt = 1.f / 60.f) {
        UpdateResult result;
        if (!render_)
            return result;
        if (width <= 0 || height <= 0)
            return result;

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
                RenderContext ctx(this);
                vnode = render_();
            }
            if (vnode.isEmpty)
                return result;

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

        return result;
    }

    // Access render root for rendering
    Node* root() const { return renderRoot_.get(); }

    // Event handling - returns true if event was consumed
    bool handleMouseDown(float x, float y, MouseButton btn) {
        if (!renderRoot_)
            return false;
        return eventHandler_.handleMouseDown(renderRoot_.get(), x, y, btn);
    }

    bool handleMouseUp(float x, float y, MouseButton btn) {
        if (!renderRoot_)
            return false;
        return eventHandler_.handleMouseUp(renderRoot_.get(), x, y, btn);
    }

    void handleMouseMove(float x, float y) {
        if (!renderRoot_)
            return;
        eventHandler_.handleMouseMove(renderRoot_.get(), x, y);
    }

    bool handleScroll(float x, float y, float dx, float dy) {
        if (!renderRoot_)
            return false;
        bool consumed = eventHandler_.handleScroll(renderRoot_.get(), x, y, dx, dy);
        if (consumed)
            dirty_ = true;
        return consumed;
    }

    void handleMouseLeave() {
        if (!renderRoot_)
            return;
        eventHandler_.handleMouseMove(renderRoot_.get(), -1, -1);
    }

    bool handleKeyDown(int keyCode, uint16_t keyMod, bool repeat = false) {
        if (!renderRoot_)
            return false;
        bool consumed = eventHandler_.handleKeyDown(renderRoot_.get(), keyCode, keyMod, repeat);
        if (consumed)
            dirty_ = true;
        return consumed;
    }

    bool handleKeyUp(int keyCode, uint16_t keyMod) {
        if (!renderRoot_)
            return false;
        bool consumed = eventHandler_.handleKeyUp(renderRoot_.get(), keyCode, keyMod);
        if (consumed)
            dirty_ = true;
        return consumed;
    }

    void handleTextInput(const std::string& text) {
        eventHandler_.handleTextInput(text);
    }

    void handleBackspace() {
        eventHandler_.handleBackspace();
    }

    void handleSubmit() { eventHandler_.handleSubmit(); }

    InputNode* getFocusedInput() const { return eventHandler_.getFocusedInput(); }

    bool hasClickHandler(float x, float y, MouseButton btn) {
        if (!renderRoot_)
            return false;
        return eventHandler_.hasClickHandler(renderRoot_.get(), x, y, btn);
    }

protected:
    std::function<VNode()> render_;
    Reconciler reconciler_;
    EventHandler eventHandler_;

    // Two trees
    std::unique_ptr<Fiber> fiberRoot_;
    std::unique_ptr<Node> renderRoot_;

    float lastWidth_ = 0;
    float lastHeight_ = 0;
    bool dirty_ = true;
    bool componentsDirty_ = false;
};

// Check if a host is still alive (for Store notification)
inline bool isHostLive(Host* host) {
    std::lock_guard lock(detail::liveHostsMutex);
    return detail::liveHosts.count(host) > 0;
}

}  // namespace yui
