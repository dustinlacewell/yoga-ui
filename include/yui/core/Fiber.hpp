#pragma once

#include "VNode.hpp"

#include <any>
#include <functional>
#include <memory>
#include <vector>

namespace yui {

// Forward declarations
class Node;
class Host;

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
    std::vector<std::function<void()>> subscriptionCleanups;
    std::vector<std::function<void()>> effectCleanups;
    std::vector<std::function<std::function<void()>()>> pendingEffects;
    Host* host = nullptr;
    bool dirty = false;

#ifndef NDEBUG
    size_t expectedHookCount = 0;
    const char* debugName = nullptr;
#endif

    // --- Methods ---
    void runCleanups();   // Clean up this fiber's effects and subscriptions (non-recursive)
    void willUnmount();   // Recursive: runCleanups() on this fiber and all descendants
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
