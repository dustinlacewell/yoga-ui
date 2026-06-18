#pragma once

#include "Fiber.hpp"
#include "Host.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace yui {

// Reactive store - call use() in render to subscribe, set() to notify
template <typename T>
class Store {
public:
    explicit Store(T value = T{}) : value_(std::move(value)) {}

    // A fiber's subscription cleanup (stored in the fiber) may outlive this
    // Store — e.g. a Store declared after the Host is destroyed first, then
    // ~Host runs the fiber cleanups. The cleanup captures a liveness token so it
    // becomes a no-op once the Store is gone, instead of touching freed members.
    ~Store() {
        std::lock_guard lock(mutex_);
        *alive_ = false;
    }

    // Read + subscribe (use in render)
    // If inside a component, subscribes the fiber (selective re-render)
    // Otherwise subscribes the host (full re-render)
    const T& use() const {
        std::lock_guard lock(mutex_);
        if (currentRenderFiber) {
            // Inside a component - subscribe just this fiber
            auto [it, inserted] = fiberSubscribers_.insert(currentRenderFiber);
            if (inserted) {
                Fiber* fiber = currentRenderFiber;
                auto alive = alive_;
                fiber->subscriptionCleanups.push_back([this, fiber, alive] {
                    if (!*alive) return;  // Store already destroyed — nothing to unsubscribe
                    std::lock_guard lock(mutex_);
                    fiberSubscribers_.erase(fiber);
                });
            }
        } else if (currentRenderHost) {
            // Top-level render - subscribe the whole host
            hostSubscribers_.insert(currentRenderHost);
        }
        return value_;
    }

    // Read without subscribing (use outside render)
    const T& peek() const {
        std::lock_guard lock(mutex_);
        return value_;
    }

    // Write - triggers re-render of subscribers
    void set(T value) {
        std::lock_guard lock(mutex_);
        value_ = std::move(value);
        notify();
    }

    // Mutate in place - triggers re-render of subscribers
    void set(const std::function<void(T&)>& mutator) {
        std::lock_guard lock(mutex_);
        mutator(value_);
        notify();
    }

private:
    void notify() {
        // Called with mutex_ held
        for (auto* fiber : fiberSubscribers_) {
            fiber->markDirty();
        }
        fiberSubscribers_.clear();

        for (auto* host : hostSubscribers_) {
            if (isHostLive(host)) {
                host->markDirty();
            }
        }
        hostSubscribers_.clear();
    }

    T value_;
    mutable std::mutex mutex_;
    mutable std::unordered_set<Fiber*> fiberSubscribers_;
    mutable std::unordered_set<Host*> hostSubscribers_;
    // Liveness token shared with outstanding fiber-cleanup lambdas; cleared in
    // the destructor so they no-op rather than dereference a freed Store.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace yui
