#pragma once

#include <yui/core/Measure.hpp>
#include <yui/detail/Reconciler.hpp>

#include <functional>
#include <memory>
#include <type_traits>

#include <yoga/Yoga.h>

namespace yui::test {

// An ITextMeasurer backed by a plain function, for driving layout in tests
// without a real rendering backend. Also tracks how many times it was invoked
// so tests can assert a measurer was (or was not) called.
class FnMeasurer : public ITextMeasurer {
public:
    using Fn = std::function<Size(const std::string& text, float fontSize, float maxWidth)>;

    explicit FnMeasurer(Fn fn) : fn_(std::move(fn)) {}

    Size measure(const std::string& text, float fontSize, float maxWidth) const override {
        ++calls_;
        return fn_(text, fontSize, maxWidth);
    }

    int calls() const { return calls_; }
    void resetCalls() { calls_ = 0; }

private:
    Fn fn_;
    mutable int calls_ = 0;
};

// Owns a per-host yoga config plus a reconciler wired to it, mirroring how a
// real Host plumbs measurement: nodes are created against config_, and the
// installed ITextMeasurer is reached through the config's context. This lets
// tests exercise the same path the production Host uses, but without a window.
class MeasureHarness {
public:
    MeasureHarness() { reconciler_.setConfig(config_.get()); }

    // Install (or clear, with nullptr) the text measurer for this harness.
    void setMeasurer(ITextMeasurer* measurer) { YGConfigSetContext(config_.get(), measurer); }

    Reconciler& reconciler() { return reconciler_; }
    YGConfigRef config() const { return config_.get(); }

    // Mount a tree and return the render root (owned by the reconciler).
    Node* mount(const VNode& tree) {
        fiber_ = reconciler_.mount(tree);
        return reconciler_.renderRoot();
    }

    Fiber* fiber() const { return fiber_.get(); }

private:
    struct ConfigDeleter {
        void operator()(YGConfigRef c) const { YGConfigFree(c); }
    };
    std::unique_ptr<std::remove_pointer_t<YGConfigRef>, ConfigDeleter> config_{YGConfigNew()};
    Reconciler reconciler_;
    std::unique_ptr<Fiber> fiber_;
};

}  // namespace yui::test
