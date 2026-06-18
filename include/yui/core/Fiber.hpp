#pragma once

#include "VNode.hpp"

#include <any>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace yui {

// Forward declarations
class Node;
class Host;

// One Store subscription held by a component fiber. A subscription is a pair of
// inverse actions over the Store's fiberSubscribers_ set, plus the store's
// identity:
//   - unsubscribe: remove this fiber from the store (teardown / re-render reset)
//   - resubscribe: re-add this fiber to the store
// Both are idempotent (set insert/erase) and liveness-guarded (no-op once the
// Store is gone), mirroring the Store/Host alive_ idiom. The pair lets the
// re-render path restore the EXACT pre-render membership when a render throws:
// Store::set() consumed (cleared) the triggering store's membership before the
// re-render ran, so a throw-before-use() would otherwise leave the fiber
// permanently unsubscribed — resubscribe re-arms it. `store` identity lets a
// successful re-render dedupe old-vs-new so a still-used store is not torn down.
struct SubscriptionRecord {
    const void* store = nullptr;            // Store identity (for old-vs-new dedupe)
    std::function<void()> resubscribe;      // re-add fiber to the store's set
    std::function<void()> unsubscribe;      // remove fiber from the store's set
};

// A move-enabled atomic<bool>. std::atomic has deleted copy/move, which would
// implicitly delete Fiber's move assignment — but remountRoot relies on
// `*fiber = std::move(*fresh)` (host thread, single-threaded at that point).
// The value, not the atomic object, is what must travel; move transfers it with
// relaxed ordering (consistent with every other access) and leaves the source
// cleared. load/store mirror std::atomic so call sites read identically.
struct MovableAtomicBool {
    std::atomic<bool> value;

    MovableAtomicBool(bool v = false) : value(v) {}
    MovableAtomicBool(MovableAtomicBool&& other) noexcept
        : value(other.value.exchange(false, std::memory_order_relaxed)) {}
    MovableAtomicBool& operator=(MovableAtomicBool&& other) noexcept {
        value.store(other.value.exchange(false, std::memory_order_relaxed),
                    std::memory_order_relaxed);
        return *this;
    }
    MovableAtomicBool(const MovableAtomicBool&) = delete;
    MovableAtomicBool& operator=(const MovableAtomicBool&) = delete;

    bool load(std::memory_order order = std::memory_order_seq_cst) const {
        return value.load(order);
    }
    void store(bool v, std::memory_order order = std::memory_order_seq_cst) {
        value.store(v, order);
    }
};

struct Fiber {
    enum class Tag { Host, Component };
    Tag tag;

    // --- Identity (for reconciliation matching) ---
    std::string key;
    int64_t intKey = INT64_MIN;
    size_t sourcePosition = SIZE_MAX;

    static constexpr int64_t NO_INT_KEY = INT64_MIN;
    static constexpr size_t NO_SOURCE_POSITION = SIZE_MAX;

    // --- Tree structure ---
    Fiber* parent = nullptr;
    std::vector<std::unique_ptr<Fiber>> children;

    // --- Host fiber fields ---
    // Non-owning pointer to the render node. The render tree owns Nodes via
    // unique_ptr in the parent Node's children vector. Only the Reconciler
    // creates, moves, or destroys render nodes.
    Node* renderNode = nullptr;

    // --- Component fiber fields ---
    ComponentFn componentFn;
    std::vector<std::any> hookState;
    std::vector<SubscriptionRecord> subscriptionCleanups;
    std::vector<std::function<void()>> effectCleanups;
    std::vector<std::function<std::function<void()>()>> pendingEffects;
    Host* host = nullptr;
    // Cross-thread: written by a worker via Store::set() -> markDirty() under
    // Store::mutex_, but read/cleared on the host thread in the Reconciler without
    // that mutex. Atomic for the same reason as Host::dirty_/componentsDirty_.
    MovableAtomicBool dirty{false};

#ifndef NDEBUG
    size_t expectedHookCount = 0;
    const char* debugName = nullptr;
#endif

    // Liveness token shared with callbacks that outlive this fiber — notably the
    // useState setter, which a consumer may store in a sibling's handler, a Store,
    // a timer, or an async completion. Cleared in ~Fiber so those callbacks no-op
    // instead of dereferencing a freed fiber. Mirrors the isHostLive / Store alive_
    // pattern: observe via a copy, verify liveness before touching the fiber.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    Fiber() = default;
    ~Fiber() {
        if (alive) *alive = false;  // null after a move-out (see remountRoot)
    }

    // Move-only (children are unique_ptr-owned). remountRoot move-assigns a fresh
    // fiber into the root; a user-declared destructor would otherwise suppress the
    // implicit move ops and fall back to a (deleted) copy.
    Fiber(Fiber&&) = default;
    Fiber& operator=(Fiber&&) = default;
    Fiber(const Fiber&) = delete;
    Fiber& operator=(const Fiber&) = delete;

    // --- Methods ---
    // runCleanups/willUnmount fire from destructors (~Host, ~Fiber) and the
    // unmount path; they are noexcept and isolate each user cleanup internally so
    // one throwing cleanup neither skips the rest nor terminates during teardown.
    void runCleanups() noexcept;   // Clean up this fiber's effects and subscriptions (non-recursive)
    void willUnmount() noexcept;   // Recursive: runCleanups() on this fiber and all descendants
    // Run+clear ONLY the subscription cleanups (Store unsubscribes), leaving effect
    // cleanups in place. Used by the re-render path to reset store membership
    // before the fresh render re-subscribes. noexcept / per-cleanup isolated.
    void runSubscriptionCleanups() noexcept;
    void markDirty();
    void runPendingEffects();

    // Identity helpers
    bool hasKey() const { return intKey != NO_INT_KEY || !key.empty(); }
    bool hasIntKey() const { return intKey != NO_INT_KEY; }
    bool hasStringKey() const { return !key.empty(); }

    bool isHost() const { return tag == Tag::Host; }
    bool isComponent() const { return tag == Tag::Component; }
};

}  // namespace yui
