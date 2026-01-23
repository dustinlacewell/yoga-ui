#pragma once

#include "Host.hpp"

#include <functional>
#include <mutex>
#include <unordered_set>

namespace yui {

// Reactive store - call use() in render to subscribe, set() to notify
template <typename T>
class Store {
public:
    explicit Store(T value = T{}) : value_(std::move(value)) {}

    // Read + subscribe (use in render)
    const T& use() const {
        std::lock_guard lock(mutex_);
        if (currentRenderHost) {
            subscribers_.insert(currentRenderHost);
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
        for (auto* host : subscribers_) {
            if (isHostLive(host)) {
                host->markDirty();
            }
        }
        subscribers_.clear();
    }

    T value_;
    mutable std::mutex mutex_;
    mutable std::unordered_set<Host*> subscribers_;
};

}  // namespace yui
