#pragma once

#include "Fiber.hpp"
#include "Host.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace yui {

// Reactive store - call use() in render to subscribe, set() to notify.
//
// THREADING CONTRACT
// ------------------
// yui is single-threaded (see Host's threading contract). use() and peek() must
// run on the host thread — use() reads thread_local render context and touches
// the subscriber sets, so calling it off-thread is meaningless and unsafe.
//
// set() is the ONE sanctioned cross-thread entry point in the whole library.
// You may call Store::set() from any thread (worker, audio, network callback).
// set() only flags the host/fibers dirty: the Host dirty flags it marks are
// atomic, and host liveness is checked under a mutex. set() does NOT reconcile
// or render on the calling thread — the re-render is applied on the host thread
// at its next update(). So a cross-thread set() is safe and its effect becomes
// visible on screen the next time the host thread runs update().
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
            // Inside a component - subscribe just this fiber. The insert is the
            // live subscription for THIS render; it is idempotent (set keyed by
            // Fiber*).
            fiberSubscribers_.insert(currentRenderFiber);

            // Record the subscription unconditionally — even when membership
            // already existed. The re-render path (rerenderComponent) relies on
            // every render's use() producing a record so it can: (a) on a thrown
            // render, restore the exact pre-render membership via resubscribe; and
            // (b) on a successful render, dedupe old-vs-new records by `store`
            // identity. A redundant record (same store use()'d twice in one
            // render) is harmless: insert/erase are both idempotent. The old
            // membership-gated append broke (a) — a use()-after-snapshot saw the
            // fiber as already-member and skipped the record, so the snapshot
            // restore could not re-arm it.
            Fiber* fiber = currentRenderFiber;
            auto alive = alive_;
            const Store* self = this;
            fiber->subscriptionCleanups.push_back(SubscriptionRecord{
                self,
                [self, fiber, alive] {  // resubscribe
                    if (!*alive) return;  // Store destroyed — nothing to re-arm
                    std::lock_guard lock(self->mutex_);
                    self->fiberSubscribers_.insert(fiber);
                },
                [self, fiber, alive] {  // unsubscribe
                    if (!*alive) return;  // Store destroyed — nothing to unsubscribe
                    std::lock_guard lock(self->mutex_);
                    self->fiberSubscribers_.erase(fiber);
                }});
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

    // Write - triggers re-render of subscribers.
    // The sole sanctioned cross-thread entry point: callable from any thread.
    // Marks subscribers dirty (atomic host flags + mutex-guarded host liveness);
    // the re-render itself runs on the host thread at the next Host::update().
    //
    // The lock guards ONLY the value mutation. notify() runs UNLOCKED: it marks
    // subscribers dirty via atomics, and re-rendering is deferred to the host
    // thread's next update() — none of it needs value_ held. Crucially this keeps
    // the lock off any user code reachable from a set(): a set() whose effect
    // re-renders a component that itself calls use()/peek()/set() on this same
    // store would self-deadlock on a non-recursive mutex if the lock spanned
    // notify(). Narrow critical section, not a recursive_mutex.
    void set(T value) {
        diagnoseSetDuringRender();
        {
            std::lock_guard lock(mutex_);
            value_ = std::move(value);
        }
        notify();
    }

    // Mutate in place - triggers re-render of subscribers.
    // Same threading contract as set(T): callable from any thread. The mutator
    // runs under the lock (it touches value_); notify() runs after the lock is
    // released so the mutator's resulting re-render can re-enter the store safely.
    void set(const std::function<void(T&)>& mutator) {
        diagnoseSetDuringRender();
        {
            std::lock_guard lock(mutex_);
            mutator(value_);
        }
        notify();
    }

private:
    // A set() inside a component body (or top-level render) re-dirties the very
    // fiber/host being rendered, so the next frame renders again and sets again:
    // an unconditional set-in-render is a per-frame re-render livelock. Detect it
    // via the render-context thread_locals the reconciler installs, and route ONE
    // deduped diagnostic through the host's existing error sink. We diagnose and
    // proceed (the set still applies) rather than defer — the actionable signal is
    // the warning; deferral would hide a genuine logic error behind silence.
    void diagnoseSetDuringRender() noexcept {
        // A fiber carries a DirtyScheduler*; a top-level host is a Host* that
        // upcasts to one. Either way we only need the diagnostic sink.
        DirtyScheduler* host = currentRenderFiber ? currentRenderFiber->host : currentRenderHost;
        if (!host) return;  // not rendering on this thread — normal set()
        if (setDuringRenderReported_.exchange(true)) return;  // already warned once
        host->reportError(
            "Store::set() called during render (likely unconditional set in a "
            "component body) — this will re-render every frame",
            nullptr);
    }

    void notify() {
        // Run UNLOCKED. Snapshot the subscriber sets under a brief lock, then
        // mark dirty on the copies: markDirty() only flips atomic flags and never
        // re-enters this store, but taking a copy keeps the iteration safe against
        // a concurrent use()/set() mutating the live sets, and lets us clear the
        // live sets atomically with the snapshot (consume-once semantics).
        std::unordered_set<Fiber*> fibers;
        std::unordered_set<Host*> hosts;
        {
            std::lock_guard lock(mutex_);
            fibers.swap(fiberSubscribers_);
            hosts.swap(hostSubscribers_);
        }

        for (auto* fiber : fibers) {
            fiber->markDirty();
        }
        for (auto* host : hosts) {
            if (isHostLive(host)) {
                host->markDirty();
            }
        }
    }

    T value_;
    mutable std::mutex mutex_;
    mutable std::unordered_set<Fiber*> fiberSubscribers_;
    mutable std::unordered_set<Host*> hostSubscribers_;
    // One-shot latch for the set-during-render diagnostic: fires once per Store so
    // a livelocking unconditional-set-in-render warns a single time, not per frame.
    std::atomic<bool> setDuringRenderReported_{false};
    // Liveness token shared with outstanding fiber-cleanup lambdas; cleared in
    // the destructor so they no-op rather than dereference a freed Store.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace yui
