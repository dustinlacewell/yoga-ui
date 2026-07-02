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
    // The function still takes (text, fontSize, maxWidth) — existing tests
    // construct it that way and don't care about the face. Only .width is
    // consumed: measureRun is the wrapping primitive sizing is built on, and
    // heights come from fontMetrics.
    using Fn = std::function<Size(const std::string& text, float fontSize, float maxWidth)>;

    explicit FnMeasurer(Fn fn) : fn_(std::move(fn)) {}

    // The run primitive the shared wrap layer (and the final measure()) is
    // built on. Counted, so tests can assert the measurer was (or was not)
    // reached from layout.
    float measureRun(std::string_view run, float fontSize, std::string_view /*font*/) const override {
        ++calls_;
        return fn_(std::string(run), fontSize, 0).width;
    }

    // lineHeight == fontSize matches what measure() reports for height, so
    // layout expectations written against fn-based heights keep holding.
    FontMetrics fontMetrics(float fontSize, std::string_view /*font*/) const override {
        return {0.8f * fontSize, 0.2f * fontSize, fontSize};
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

    // Mount a tree and return the render root (owned by the reconciler). Takes
    // the tree by value and moves it into the reconciler's rvalue mount(), so
    // callers passing a named lvalue still compile (the copy stops here).
    Node* mount(VNode tree) {
        fiber_ = reconciler_.mount(std::move(tree));
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
