#pragma once

#include <yui/core/ComponentContext.hpp>
#include <yui/core/Node.hpp>
#include <yui/core/VNode.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace yui::widgets {

// Slider: a controlled horizontal value slider (value in 0..1, app owns it).
//
// Composition (a stateful COMPONENT, not a pure builder — it needs an element
// ref to map pointer pixels back to value):
//
//   Box( track-layer, thumb-layer )      <- wrapper: the press target / captor
//     .ref(trackRef) + the 3 mouse handlers + optional onKeyDown
//
// The wrapper carries all three pointer handlers, so it is the press target and
// therefore the IMPLICIT CAPTOR: once a press lands on it, onMouseMove is routed
// to it while the button is held (even off-node / off-window) and onMouseUp is
// delivered to it wherever the release happens. That capture is what makes a
// drag past the track edge clamp cleanly to 0/1 instead of dropping the stream.
//
// Value flows in; onChange flows out; the app re-renders with the new value. The
// `dragging` flag is a useRef (NOT useState): flipping drag state must not mark
// the fiber dirty — the visual update comes from the app's onChange-driven
// re-render, and onMouseMove ALSO fires on plain hover, so the flag is the gate
// that distinguishes a real drag from a passing pointer.

// --- Default chrome (override via setters; no theme dependency) ---
inline constexpr uint32_t kSliderTrack = 0x2A2A2AFFu;
inline constexpr uint32_t kSliderFill = 0x4A90D9FFu;
inline constexpr uint32_t kSliderThumb = 0xFFFFFFFFu;
inline constexpr float kSliderTrackHeight = 4.0f;
inline constexpr float kSliderThumbSize = 14.0f;
inline constexpr float kSliderKeyStep = 0.05f;

namespace slider_detail {

inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// Pixel -> value. The wrapper's drawn rect r and the pointer x map to a value in
// 0..1. The thumb is thumbSize wide and its CENTER tracks the value, so its
// center travels only across (r.w - thumbSize) pixels, offset by thumbSize/2 at
// each end. Using the same offset here makes a click on the thumb map back to
// the current value (no jump). A degenerate track (w <= thumbSize) maps to 0.
inline float valueFromX(float x, const layout::Rect& r, float thumbSize) {
    const float travel = r.w - thumbSize;
    if (travel <= 0.0f)
        return 0.0f;
    return clamp01((x - r.x - thumbSize / 2.0f) / travel);
}

// Quantize to the nearest step (in value space), then re-clamp. Unset = pass
// through continuous.
inline float applyStep(float v, std::optional<float> step) {
    if (!step || *step <= 0.0f)
        return v;
    return clamp01(std::round(v / *step) * *step);
}

}  // namespace slider_detail

class SliderBuilder {
public:
    explicit SliderBuilder(float value) : value_(slider_detail::clamp01(value)) {}

    // Fires on the press-jump and on every drag move that CHANGES the value.
    SliderBuilder& onChange(std::function<void(float)> fn) {
        onChange_ = std::move(fn);
        return *this;
    }

    // Optional quantization in value space (e.g. .step(0.1)). Unset = continuous.
    SliderBuilder& step(float s) {
        step_ = s;
        return *this;
    }

    SliderBuilder& disabled(bool v = true) {
        disabled_ = v;
        return *this;
    }

    SliderBuilder& trackColor(uint32_t c) {
        trackColor_ = c;
        return *this;
    }
    SliderBuilder& fillColor(uint32_t c) {
        fillColor_ = c;
        return *this;
    }
    SliderBuilder& thumbColor(uint32_t c) {
        thumbColor_ = c;
        return *this;
    }
    SliderBuilder& trackHeight(float h) {
        trackHeight_ = h;
        return *this;
    }
    SliderBuilder& thumbSize(float s) {
        thumbSize_ = s;
        return *this;
    }

    // Opt-in keyboard nav: app supplies its toolkit's keycodes (core is
    // keycode-agnostic, the Modal precedent). When neither is set, NO onKeyDown
    // is attached — attaching one would consume every key bubbling through the
    // focused slider.
    SliderBuilder& decrementKeyCode(int keyCode) {
        decrementKey_ = keyCode;
        return *this;
    }
    SliderBuilder& incrementKeyCode(int keyCode) {
        incrementKey_ = keyCode;
        return *this;
    }
    SliderBuilder& keyStep(float s) {
        keyStep_ = s;
        return *this;
    }

    // --- Conversion seam (Child-only, as a Component; the Tooltip idiom) ---
    operator Child() const& { return Child{build()}; }
    operator Child() && { return Child{build()}; }

private:
    Component build() const {
        return Component([value = value_, onChange = onChange_, step = step_, disabled = disabled_,
                          trackColor = trackColor_, fillColor = fillColor_, thumbColor = thumbColor_,
                          trackHeight = trackHeight_, thumbSize = thumbSize_, decrementKey = decrementKey_,
                          incrementKey = incrementKey_, keyStep = keyStep_](ComponentContext& ctx) -> VNode {
                   using namespace slider_detail;

                   // Drag state. useRef (not useState) because flipping it must NOT
                   // dirty the fiber (see class note). The returned bool& is HEAP
                   // storage owned by the hook slot's shared_ptr (see
                   // ComponentContext.hpp useRef): created once on first render and
                   // returned every render, so it is stable across later hooks in
                   // this render AND across re-renders — handlers may capture
                   // `&dragging` safely.
                   bool& dragging = ctx.useRef<bool>(false);
                   NodeRef trackRef = ctx.useElementRef();

                   const float v = clamp01(value);

                   // Track layer: full-width track with an inset fill bar sized to
                   // v. Absolutely positioned, vertically centered in the wrapper.
                   VNode trackLayer = Box(Box()
                                              .widthPercent(v * 100.0f)
                                              .heightPercent(100.0f)
                                              .backgroundColor(fillColor)
                                              .borderRadius(trackHeight / 2.0f)
                                              .setKey("fill"))
                                          .positionType(PositionType::Absolute)
                                          .positionLeft(0)
                                          .positionTop((thumbSize - trackHeight) / 2.0f)
                                          .widthPercent(100.0f)
                                          .height(trackHeight)
                                          .backgroundColor(trackColor)
                                          .borderRadius(trackHeight / 2.0f);

                   // Thumb layer: positionLeft is PIXELS only, so the thumb is
                   // placed by flexGrow RATIOS (v : 1-v) around a fixed-size
                   // thumb — a percentage-free way to track the value.
                   VNode thumbLayer = Row(Box().flexGrow(v),
                                          Box()
                                              .width(thumbSize)
                                              .height(thumbSize)
                                              .borderRadius(thumbSize / 2.0f)
                                              .backgroundColor(thumbColor)
                                              .setKey("thumb"),
                                          Box().flexGrow(1.0f - v))
                                          .widthPercent(100.0f);

                   auto wrapper = Box(std::move(trackLayer), std::move(thumbLayer))
                                      .height(thumbSize)
                                      .widthPercent(100.0f)
                                      .ref(trackRef)
                                      .cursor(disabled ? CursorShape::Arrow : CursorShape::Pointer)
                                      .focusable(!disabled)
                                      .setKey("slider");

                   // All three handlers sit on the wrapper = press target = the
                   // implicit captor. onMouseMove ALSO fires on plain hover, so
                   // the dragging flag gates the move path.
                   wrapper.onMouseDown([trackRef, &dragging, onChange, step, thumbSize,
                                        disabled](float x, float /*y*/, MouseButton btn, uint16_t /*mods*/) {
                       if (disabled || btn != MouseButton::Left)
                           return;
                       dragging = true;
                       layout::Rect r = trackRef.getBoundingRect();
                       float nv = applyStep(valueFromX(x, r, thumbSize), step);
                       if (onChange)
                           onChange(nv);
                   });

                   wrapper.onMouseMove([trackRef, &dragging, onChange, step, thumbSize,
                                        last = v](float x, float /*y*/) mutable {
                       if (!dragging)  // plain hover, no press: ignore
                           return;
                       layout::Rect r = trackRef.getBoundingRect();
                       float nv = applyStep(valueFromX(x, r, thumbSize), step);
                       if (onChange && nv != last) {  // only on a real change (step dedup)
                           last = nv;
                           onChange(nv);
                       }
                   });

                   wrapper.onMouseUp(
                       [&dragging](float /*x*/, float /*y*/, MouseButton /*btn*/) { dragging = false; });

                   // Opt-in keyboard: only attach onKeyDown when a keycode is set
                   // (attaching one consumes every key while focused — documented
                   // gotcha). Dec/inc nudge by keyStep, clamped.
                   if (decrementKey || incrementKey) {
                       wrapper.onKeyDown([onChange, decrementKey, incrementKey, keyStep, v = v](
                                             int keyCode, uint16_t /*mods*/, bool /*repeat*/) {
                           if (!onChange)
                               return;
                           if (decrementKey && keyCode == *decrementKey)
                               onChange(clamp01(v - keyStep));
                           else if (incrementKey && keyCode == *incrementKey)
                               onChange(clamp01(v + keyStep));
                       });
                   }

                   return wrapper;
               })
            .setName("Slider");
    }

    float value_;
    std::function<void(float)> onChange_;
    std::optional<float> step_;
    bool disabled_ = false;
    uint32_t trackColor_ = kSliderTrack;
    uint32_t fillColor_ = kSliderFill;
    uint32_t thumbColor_ = kSliderThumb;
    float trackHeight_ = kSliderTrackHeight;
    float thumbSize_ = kSliderThumbSize;
    std::optional<int> decrementKey_;
    std::optional<int> incrementKey_;
    float keyStep_ = kSliderKeyStep;
};

// --- Factory ---

// Slider at `value` (0..1, clamped).
[[nodiscard]] inline SliderBuilder Slider(float value) { return SliderBuilder{value}; }

}  // namespace yui::widgets
