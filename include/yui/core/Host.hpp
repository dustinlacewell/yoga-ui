#pragma once

#include "EventHandler.hpp"
#include "Node.hpp"
#include "Reconciler.hpp"
#include "VNode.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace yui {

class Host;

// Global registry of live hosts for safe Store notification
namespace detail {
inline std::unordered_set<Host*> liveHosts;
inline std::mutex liveHostsMutex;
}  // namespace detail

// Current render context - set during render() for Store subscriptions
inline thread_local Host* currentRenderHost = nullptr;

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

// Base class for yui host implementations.
// Provides core reconciliation, layout, and event handling.
// Subclasses implement platform-specific rendering and event forwarding.
class Host {
public:
    Host() {
        std::lock_guard lock(detail::liveHostsMutex);
        detail::liveHosts.insert(this);

        reconciler_.setNodeRemovedCallback([this](Node* node) { eventHandler_.onNodeRemoved(node); });
    }

    virtual ~Host() {
        {
            std::lock_guard lock(detail::liveHostsMutex);
            detail::liveHosts.erase(this);
        }
        if (root_) {
            root_->willUnmount();
        }
    }

    // Non-copyable, non-movable
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // Set the render function
    void setRender(std::function<VNode()> renderFn) {
        render_ = std::move(renderFn);
        markDirty();
    }

    // Mark for re-render on next update
    void markDirty() { dirty_ = true; }

    bool isDirty() const { return dirty_; }

    // Call each frame to update animations, reconcile if dirty, and layout
    void update(float width, float height, float dt = 1.f / 60.f) {
        if (!render_)
            return;
        if (width <= 0 || height <= 0)
            return;

        // Update animations every frame
        if (root_) {
            root_->update(dt);
        }

        // Check if size changed
        if (width != lastWidth_ || height != lastHeight_) {
            lastWidth_ = width;
            lastHeight_ = height;
            dirty_ = true;
        }

        if (!dirty_)
            return;
        dirty_ = false;

        VNode vnode;
        {
            RenderContext ctx(this);
            vnode = render_();
        }
        if (vnode.isEmpty)
            return;

        if (!root_) {
            root_ = reconciler_.mount(vnode);
        } else {
            reconciler_.reconcile(root_.get(), vnode);
        }

        if (root_) {
            root_->calculateLayout(width, height);
        }
    }

    // Access root for rendering
    Node* root() const { return root_.get(); }

    // Event handling - returns true if event was consumed
    bool handleMouseDown(float x, float y, MouseButton btn) {
        if (!root_)
            return false;
        return eventHandler_.handleMouseDown(root_.get(), x, y, btn);
    }

    bool handleMouseUp(float x, float y, MouseButton btn) {
        if (!root_)
            return false;
        return eventHandler_.handleMouseUp(root_.get(), x, y, btn);
    }

    void handleMouseMove(float x, float y) {
        if (!root_)
            return;
        eventHandler_.handleMouseMove(root_.get(), x, y);
    }

    bool handleScroll(float x, float y, float dx, float dy) {
        if (!root_)
            return false;
        bool consumed = eventHandler_.handleScroll(root_.get(), x, y, dx, dy);
        if (consumed)
            dirty_ = true;
        return consumed;
    }

    void handleMouseLeave() {
        if (!root_)
            return;
        eventHandler_.handleMouseMove(root_.get(), -1, -1);
    }

    // Keyboard events - dispatched to focused node or root
    bool handleKeyDown(int keyCode, uint16_t keyMod, bool repeat = false) {
        if (!root_)
            return false;
        bool consumed = eventHandler_.handleKeyDown(root_.get(), keyCode, keyMod, repeat);
        if (consumed)
            dirty_ = true;
        return consumed;
    }

    bool handleKeyUp(int keyCode, uint16_t keyMod) {
        if (!root_)
            return false;
        bool consumed = eventHandler_.handleKeyUp(root_.get(), keyCode, keyMod);
        if (consumed)
            dirty_ = true;
        return consumed;
    }

    // Text input - for text input fields
    void handleTextInput(const std::string& text) {
        eventHandler_.handleTextInput(text);
        dirty_ = true;
    }

    void handleBackspace() {
        eventHandler_.handleBackspace();
        dirty_ = true;
    }

    void handleSubmit() { eventHandler_.handleSubmit(); }

    // Focus management
    InputNode* getFocusedInput() const { return eventHandler_.getFocusedInput(); }

    bool hasClickHandler(float x, float y, MouseButton btn) {
        if (!root_)
            return false;
        return eventHandler_.hasClickHandler(root_.get(), x, y, btn);
    }

protected:
    std::function<VNode()> render_;
    Reconciler reconciler_;
    EventHandler eventHandler_;
    std::unique_ptr<Node> root_;
    float lastWidth_ = 0;
    float lastHeight_ = 0;
    bool dirty_ = true;
};

// Check if a host is still alive (for Store notification)
inline bool isHostLive(Host* host) {
    std::lock_guard lock(detail::liveHostsMutex);
    return detail::liveHosts.count(host) > 0;
}

}  // namespace yui
