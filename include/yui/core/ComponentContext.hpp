#pragma once

#include <any>
#include <functional>
#include <memory>
#include <vector>

namespace yui {

// Forward declarations
struct Fiber;
class Host;
template <typename T>
class Store;

// Context provided to component functions during render
class ComponentContext {
public:
    ComponentContext(Fiber* fiber, Host* host);
    ~ComponentContext();

    // useState hook - returns current value and setter function
    template <typename T>
    std::pair<T, std::function<void(T)>> useState(T initial);

    // useRef hook - returns a reference to persistent mutable storage
    template <typename T>
    T& useRef(T initial = T{});

    // useField hook - binds to a Store field for controlled inputs
    template <typename S, typename T>
    std::pair<const T&, std::function<void(const T&)>> useField(Store<S>& store, T S::*field);

    // useEffect hook - run side effects after render
    void useEffect(std::function<std::function<void()>()> effect);

    // Access the fiber (for advanced use cases)
    Fiber* fiber() const { return fiber_; }

private:
    Fiber* fiber_;
    Host* host_;
    size_t hookIndex_ = 0;
};

}  // namespace yui

// Include dependencies for template implementation
#include "Fiber.hpp"
#include "Host.hpp"
#include "Store.hpp"

namespace yui {

template <typename T>
std::pair<T, std::function<void(T)>> ComponentContext::useState(T initial) {
    size_t index = hookIndex_++;

    if (index >= fiber_->hookState.size()) {
        fiber_->hookState.push_back(std::move(initial));
    }

    T& value = std::any_cast<T&>(fiber_->hookState[index]);

    Fiber* captured = fiber_;
    auto setter = [captured, index](T newValue) {
        if (index < captured->hookState.size()) {
            captured->hookState[index] = std::move(newValue);
            captured->markDirty();
        }
    };

    return {value, std::move(setter)};
}

template <typename T>
T& ComponentContext::useRef(T initial) {
    size_t index = hookIndex_++;

    if (index >= fiber_->hookState.size()) {
        fiber_->hookState.push_back(std::move(initial));
    }

    return std::any_cast<T&>(fiber_->hookState[index]);
}

template <typename S, typename T>
std::pair<const T&, std::function<void(const T&)>> ComponentContext::useField(Store<S>& store, T S::*field) {
    const T& value = store.use().*field;

    auto setter = [&store, field](const T& newValue) { store.set([field, newValue](S& s) { s.*field = newValue; }); };

    return {value, std::move(setter)};
}

}  // namespace yui
