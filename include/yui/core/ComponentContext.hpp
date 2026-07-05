#pragma once

#include <any>
#include <functional>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <utility>
#include <vector>

#include "Fiber.hpp"     // HookTag
#include "NodeRef.hpp"   // NodeRef (return type of useElementRef)

namespace yui {

// Forward declarations
struct DirtyScheduler;
template <typename T>
class Store;

// Context provided to component functions during render
class ComponentContext {
public:
    ComponentContext(Fiber* fiber, DirtyScheduler* host);
    ~ComponentContext();

    // useState hook - returns current value and setter function
    template <typename T>
    std::pair<T, std::function<void(T)>> useState(T initial);

    // useRef hook - returns a reference to persistent mutable storage
    template <typename T>
    T& useRef(T initial = T{});

    // useElementRef hook - returns a stable element ref handle. Attach it to a
    // host element with BuilderBase::ref(); read the node's drawn rect via the
    // handle in an event handler (NOT during render). Returns the SAME underlying
    // slot every render (React useRef identity stability).
    NodeRef useElementRef();

    // useField hook - binds to a Store field for controlled inputs.
    // Returns the field BY VALUE: store.use() now returns a copy, so the field is
    // read out of that copy — a reference would dangle the moment a set() replaced
    // the store value.
    template <typename S, typename T>
    std::pair<T, std::function<void(const T&)>> useField(Store<S>& store, T S::*field);

    // useEffect hook - run side effects after render
    void useEffect(std::function<std::function<void()>()> effect);

    // Access the fiber (for advanced use cases)
    Fiber* fiber() const { return fiber_; }

private:
    // Append-or-check the call-order tag for the hook at `index`. On the first
    // render the tag is recorded; on every later render it is compared against the
    // recorded tag. A mismatch is a rules-of-hooks violation (the Nth hook changed
    // kind/type between renders) AND the precondition failure for the std::any_cast
    // the stateful hooks are about to perform. Both are diagnosed through the host
    // error sink here. Returns true when the tag matches (caller may proceed to
    // any_cast); false when it mismatched (caller must NOT any_cast — the slot holds
    // an unrelated type). Effects pass typeid(void) and ignore the result.
    bool checkHookTag(size_t index, HookTag::Kind kind, std::type_index type);

    Fiber* fiber_;
    DirtyScheduler* host_;
    // Two distinct index spaces, both reset to 0 per render (fresh context):
    //   hookIndex_ — the POSITIONAL/tag counter. Advanced by EVERY hook (state,
    //     ref, field, effect) so checkHookTag/hookTags see one slot per hook call,
    //     enforcing rules-of-hooks across the full call order.
    //   stateSlot_ — the hookState slot counter. Advanced ONLY by hooks that store
    //     in fiber_->hookState (useState, useRef). useField/useEffect push nothing
    //     to hookState, so advancing hookState's index for them would leave stateful
    //     hooks indexing past their true slot (OOB any_cast). Keeping the two spaces
    //     separate is what makes "useEffect/useField before useState" safe.
    size_t hookIndex_ = 0;
    size_t stateSlot_ = 0;
};

}  // namespace yui

// Include dependencies for template implementation
#include "Fiber.hpp"
#include "Host.hpp"
#include "Store.hpp"

namespace yui {

template <typename T>
std::pair<T, std::function<void(T)>> ComponentContext::useState(T initial) {
    // Tag at the positional index (advances for EVERY hook); store at the state
    // slot (advances ONLY for hooks that own a hookState entry). The two diverge
    // whenever a useField/useEffect precedes this call.
    // Guard the slot's identity BEFORE any_cast. A tag mismatch means this index
    // held a different hook/type last render — diagnose, then throw a clear error
    // instead of letting any_cast throw a context-free std::bad_any_cast.
    if (!checkHookTag(hookIndex_++, HookTag::Kind::State, std::type_index(typeid(T)))) {
        throw std::runtime_error("yui: useState hook type changed across renders (rules-of-hooks violation)");
    }

    size_t index = stateSlot_++;

    if (index >= fiber_->hookState.size()) {
        fiber_->hookState.push_back(std::move(initial));
    }

    T& value = std::any_cast<T&>(fiber_->hookState[index]);

    // The setter may be STORED and called later (in a node prop, a sibling's
    // handler), but ONLY on the host thread — the setter mutates hookState and marks
    // the fiber dirty, which are host-thread-only operations. For state that must be
    // written from another thread (a timer, a worker, an async completion), use a
    // Store (Store::set() is the sole sanctioned cross-thread write). The captured
    // fiber liveness token makes an on-thread call after unmount a safe no-op
    // instead of a use-after-free; it is NOT a license to call the setter off-thread.
    Fiber* captured = fiber_;
    auto alive = fiber_->alive;
    auto setter = [captured, alive = std::move(alive), index](T newValue) {
        if (!*alive) return;  // fiber unmounted — do not dereference it
        if (index < captured->hookState.size()) {
            captured->hookState[index] = std::move(newValue);
            captured->markDirty();
        }
    };

    return {value, std::move(setter)};
}

template <typename T>
T& ComponentContext::useRef(T initial) {
    // Tag at the positional index, store at the state slot (see useState).
    // The slot holds a shared_ptr<T> (stable heap), NOT a bare T: useRef returns a
    // T& and the caller may hold it across a LATER hook in the same render; a later
    // hook's hookState.push_back can realloc the vector, so a T& into the vector
    // would dangle. Referencing *shared_ptr is realloc-proof — same stable-slot
    // discipline useElementRef uses (its NodeRef owns a shared_ptr too). The tag
    // type is the STORED type (shared_ptr<T>), matching the any_cast target below.
    if (!checkHookTag(hookIndex_++, HookTag::Kind::Ref, std::type_index(typeid(std::shared_ptr<T>)))) {
        throw std::runtime_error("yui: useRef hook type changed across renders (rules-of-hooks violation)");
    }

    size_t index = stateSlot_++;

    if (index >= fiber_->hookState.size()) {
        fiber_->hookState.push_back(std::make_shared<T>(std::move(initial)));
    }

    return *std::any_cast<const std::shared_ptr<T>&>(fiber_->hookState[index]);
}

inline NodeRef ComponentContext::useElementRef() {
    // Occupies a positional + hookState slot like useRef. The NodeRef stored on
    // first render owns a stable slot (shared_ptr); returning a copy each render
    // hands back the SAME underlying slot, so a captured ref stays valid across
    // re-renders (React useRef identity stability).
    if (!checkHookTag(hookIndex_++, HookTag::Kind::ElementRef, std::type_index(typeid(NodeRef)))) {
        throw std::runtime_error("yui: useElementRef hook changed across renders (rules-of-hooks violation)");
    }

    size_t index = stateSlot_++;

    if (index >= fiber_->hookState.size()) {
        fiber_->hookState.push_back(NodeRef{});
    }

    return std::any_cast<NodeRef&>(fiber_->hookState[index]);
}

template <typename S, typename T>
std::pair<T, std::function<void(const T&)>> ComponentContext::useField(Store<S>& store, T S::*field) {
    // useField occupies a positional hook slot like the others, so it must advance
    // the index and tag itself — otherwise a useField<->useState reorder would slip
    // past the rules-of-hooks check. It binds to a Store rather than an any slot, so
    // a tag mismatch is diagnosed (inside checkHookTag) but NOT fatal: there is no
    // unsafe any_cast to guard, so we proceed.
    checkHookTag(hookIndex_++, HookTag::Kind::Field, std::type_index(typeid(T)));

    // use() returns a copy of the store value; take the field out of it by value.
    T value = store.use().*field;

    // The setter may outlive the Store (stored in a node prop / async callback).
    // Capture the Store's liveness token (weak) and verify it before set(), so a
    // call after the Store is destroyed is a safe no-op instead of a use-after-free.
    // Mirrors useState's fiber-alive token.
    auto token = store.aliveToken();
    auto setter = [&store, token = std::move(token), field](const T& newValue) {
        if (auto a = token.lock(); a && *a) {
            store.set([field, newValue](S& s) { s.*field = newValue; });
        }
    };

    return {value, std::move(setter)};
}

}  // namespace yui
