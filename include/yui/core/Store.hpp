#pragma once

#include "Fiber.hpp"
#include "Host.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace yui {

// Reactive store - call use() in render to subscribe, set() to notify.
//
// THREADING CONTRACT
// ------------------
// yui is single-threaded (see Host's threading contract). use() and peek() must
// run on the host thread — use() reads thread_local render context and touches
// the subscriber sets, so calling it off-thread is meaningless and unsafe. Both
// return the value BY VALUE (a copy taken under the lock), never a reference into
// value_: a returned reference would dangle the instant a cross-thread set()
// reassigns value_, so the copy is what makes the read race-free.
//
// set() is the ONE sanctioned cross-thread entry point in the whole library.
// You may call Store::set() from any thread (worker, audio, network callback).
// set() only flags the host/fibers dirty: fiber liveness is checked under the
// store mutex (so a subscriber cannot be freed between the check and markDirty),
// and host liveness is checked-and-marked under liveHostsMutex. set() does NOT
// reconcile or render on the calling thread — the re-render is applied on the
// host thread at its next update() via a release/acquire handoff of the dirty
// flags. So a cross-thread set() is safe and its effect becomes visible on
// screen the next time the host thread runs update().
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
    //
    // Returns BY VALUE: a copy taken under the lock. A reference into value_ would
    // dangle if a cross-thread set() reassigned value_ while the caller held it.
    T use() const {
        // Reentrant read from within this store's own mutator (set(mutator) whose
        // re-render re-enters use()): we already hold mutex_ on this thread, and it
        // is non-recursive, so re-locking would deadlock. Read value_ directly.
        if (mutatingThread_.load(std::memory_order_relaxed) == std::this_thread::get_id()) {
            return value_;
        }
        std::lock_guard lock(mutex_);
        if (currentRenderFiber) {
            // Inside a component - subscribe just this fiber. The insert is the
            // live subscription for THIS render; it is idempotent (map keyed by
            // Fiber*). The mapped weak token lets notify() verify the fiber is
            // still alive before marking it dirty (see notify()).
            fiberSubscribers_[currentRenderFiber] = currentRenderFiber->alive;

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
            auto fiberAlive = fiber->alive;
            auto alive = alive_;
            const Store* self = this;
            fiber->subscriptionCleanups.push_back(SubscriptionRecord{
                self,
                [self, fiber, fiberAlive, alive] {  // resubscribe
                    if (!*alive) return;  // Store destroyed — nothing to re-arm
                    std::lock_guard lock(self->mutex_);
                    self->fiberSubscribers_[fiber] = fiberAlive;
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
    //
    // Returns BY VALUE: a copy taken under the lock, for the same dangling-on-set
    // reason as use().
    T peek() const {
        // Reentrant read from within this store's own mutator (see use()).
        if (mutatingThread_.load(std::memory_order_relaxed) == std::this_thread::get_id()) {
            return value_;
        }
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
        // Reentrant write from within this store's own mutator: we already hold
        // mutex_ (non-recursive) and are mid-mutation on this thread. Assign value_
        // directly and skip notify() — the in-flight set(mutator) will notify() once
        // it unwinds, coalescing both writes into a single notification.
        if (mutatingThread_.load(std::memory_order_relaxed) == std::this_thread::get_id()) {
            value_ = std::move(value);
            return;
        }
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
        // Recursion guard (E2 — diagnose, don't deadlock). A mutator that itself
        // calls set(mutator) on THIS store would re-lock the non-recursive mutex_
        // and deadlock. Detect the reentry by thread id, route ONE diagnostic
        // through the same sink as setDuringRender, and return without re-locking.
        if (mutatingThread_.load(std::memory_order_relaxed) == std::this_thread::get_id()) {
            diagnoseReentrantMutator();
            return;
        }
        {
            std::lock_guard lock(mutex_);
            mutatingThread_.store(std::this_thread::get_id(), std::memory_order_relaxed);
            // Reset even if the mutator throws, so a later set() is not misread as
            // reentrant on this thread.
            struct ClearThread {
                std::atomic<std::thread::id>& t;
                ~ClearThread() { t.store(std::thread::id{}, std::memory_order_relaxed); }
            } clearThread{mutatingThread_};
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

    // A set(mutator) that re-enters set(mutator) on the same store from within the
    // mutator would deadlock the non-recursive mutex_. We diagnose-and-drop the
    // reentrant write (E2) rather than defer or deadlock: the reentry is a logic
    // error, and the warning is the actionable signal. Routed through whatever
    // render-context sink is available; if none, it is silently dropped (there is
    // no host to report to off the render path).
    void diagnoseReentrantMutator() noexcept {
        DirtyScheduler* host = currentRenderFiber ? currentRenderFiber->host : currentRenderHost;
        if (!host) return;
        if (reentrantMutatorReported_.exchange(true)) return;  // already warned once
        host->reportError(
            "Store::set(mutator) re-entered from within its own mutator — the "
            "reentrant write is dropped (would deadlock the store mutex)",
            nullptr);
    }

    void notify() {
        // Two-phase, with a strict lock hierarchy (Store::mutex_ -> liveHostsMutex,
        // never inverted).
        //
        // Phase 1 (under mutex_): mark every LIVE fiber subscriber dirty, then
        // consume (clear) the set. Marking under mutex_ is what makes the fiber
        // path airtight: ~Fiber runs its unsubscribe (which takes mutex_) before
        // it clears *alive, so once we hold mutex_ a subscriber we observe as live
        // (weak.lock() && *a) cannot be freed underneath the markDirty() call —
        // the dtor's erase-under-mutex_ is serialized behind us. markDirty() only
        // flips atomic flags and never re-enters this store.
        //
        // Phase 2 (NO mutex_ held): swap out the host set, then mark each live host
        // under liveHostsMutex ALONE. Running this after releasing mutex_ keeps
        // liveHostsMutex from ever nesting under mutex_.
        std::unordered_set<Host*> hosts;
        {
            std::lock_guard lock(mutex_);
            for (auto& [fiber, weak] : fiberSubscribers_) {
                if (auto a = weak.lock(); a && *a) {
                    fiber->markDirty();  // marked UNDER mutex_ (see above)
                }
            }
            fiberSubscribers_.clear();  // consume-once
            hosts.swap(hostSubscribers_);
        }

        for (auto* host : hosts) {
            detail::markHostIfLive(host);  // liveHostsMutex only — no Store::mutex_
        }
    }

public:
    // Liveness token shared with callbacks that outlive this Store (e.g. useField's
    // setter, which captures the store by reference). Observers hold a weak_ptr and
    // verify (lock() && *p) before calling set(), mirroring the Fiber/Host alive_
    // idiom. Cleared in ~Store.
    std::weak_ptr<bool> aliveToken() const { return alive_; }

private:
    T value_;
    mutable std::mutex mutex_;
    // Fiber subscribers, each mapped to a weak liveness token captured at use().
    // notify() checks the token under mutex_ before marking, so a fiber freed
    // concurrently is skipped rather than dereferenced (see notify()).
    mutable std::unordered_map<Fiber*, std::weak_ptr<bool>> fiberSubscribers_;
    mutable std::unordered_set<Host*> hostSubscribers_;
    // One-shot latch for the set-during-render diagnostic: fires once per Store so
    // a livelocking unconditional-set-in-render warns a single time, not per frame.
    std::atomic<bool> setDuringRenderReported_{false};
    // One-shot latch for the reentrant-mutator diagnostic (see diagnoseReentrantMutator).
    std::atomic<bool> reentrantMutatorReported_{false};
    // The thread currently inside set(mutator)'s locked region, or a default id.
    // use()/peek()/set() compare against it to detect a reentrant call from within
    // this store's own mutator and read/write value_ directly instead of re-locking
    // the non-recursive mutex_ (which would deadlock).
    std::atomic<std::thread::id> mutatingThread_{};
    // Liveness token shared with outstanding fiber-cleanup lambdas and useField
    // setters; cleared in the destructor so they no-op rather than dereference a
    // freed Store.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace yui
