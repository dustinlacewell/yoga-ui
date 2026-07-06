#pragma once

#include <yui/core/VNode.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace yui::widgets {

// Switch: a controlled boolean toggle drawn as a sliding pill — a rounded track
// that recolors when on, with a chip that slides from the left (off) to the
// right (on). Same controlled contract as Checkbox: `on` is passed in, the app
// owns it via onChange, the widget holds NO transient state, so the chip's
// position and the track color always mirror the value handed in this render.
// Clicking fires onChange(!on); the app flips its own state and re-renders.
//
// Geometry is derived from the track width/height so a caller can resize the
// whole control with .size(w, h). The chip is a circle inset by kSwitchPad on
// both ends; its slide distance is (width - height) so it travels flush between
// the two ends. Positioned absolutely inside the track (positionLeft is pixels).
//
// Keyboard: opt-in, mirroring Checkbox. When toggleKeyCode is set an onKeyDown
// on the focusable Row fires onChange(!on) for that keycode — note it consumes
// EVERY key while focused, so it is attached only when a keycode is set.

inline constexpr uint32_t kSwitchTrackOff = 0x808080FFu;  // track when off
inline constexpr uint32_t kSwitchTrackOn = 0x4A90D9FFu;   // track when on
inline constexpr uint32_t kSwitchChip = 0xFFFFFFFFu;      // sliding chip
inline constexpr float kSwitchWidth = 36.0f;
inline constexpr float kSwitchHeight = 20.0f;
inline constexpr float kSwitchPad = 2.0f;  // chip inset from the track edge

class SwitchBuilder {
public:
    explicit SwitchBuilder(bool on) : on_(on) {}

    SwitchBuilder& onChange(std::function<void(bool)> fn) {
        onChange_ = std::move(fn);
        return *this;
    }
    SwitchBuilder& label(std::string text) {
        label_ = std::move(text);
        return *this;
    }
    SwitchBuilder& disabled(bool v = true) {
        disabled_ = v;
        return *this;
    }
    SwitchBuilder& trackOffColor(uint32_t c) {
        trackOff_ = c;
        return *this;
    }
    SwitchBuilder& trackOnColor(uint32_t c) {
        trackOn_ = c;
        return *this;
    }
    SwitchBuilder& chipColor(uint32_t c) {
        chip_ = c;
        return *this;
    }
    SwitchBuilder& size(float w, float h) {
        width_ = w;
        height_ = h;
        return *this;
    }
    SwitchBuilder& textColor(uint32_t c) {
        textColor_ = c;
        return *this;
    }
    SwitchBuilder& fontSize(float s) {
        fontSize_ = s;
        return *this;
    }
    // App-supplied keycode that toggles while focused. Unset -> no key handler.
    SwitchBuilder& toggleKeyCode(int keyCode) {
        toggleKeyCode_ = keyCode;
        return *this;
    }

    // --- Conversion seam ---
    operator VNode() const& { return build(); }
    operator VNode() && { return build(); }
    operator Child() const& { return Child{build()}; }
    operator Child() && { return Child{build()}; }

private:
    VNode build() const {
        const bool on = on_;
        auto onChange = onChange_;
        const bool disabled = disabled_;

        // Consuming even when disabled: a switch click must never bubble into a
        // parent handler (the Checkbox/Modal consuming-no-op precedent).
        auto click = [on, onChange, disabled] {
            if (!disabled && onChange)
                onChange(!on);
        };

        const float chipSize = height_ - 2.0f * kSwitchPad;
        const float chipLeft = on ? (width_ - height_ + kSwitchPad) : kSwitchPad;

        VNode track = Box(Box()
                              .setKey("chip")
                              .width(chipSize)
                              .height(chipSize)
                              .borderRadius(chipSize / 2.0f)
                              .backgroundColor(chip_)
                              .positionType(PositionType::Absolute)
                              .positionLeft(chipLeft)
                              .positionTop(kSwitchPad))
                          .setKey("track")
                          .width(width_)
                          .height(height_)
                          .borderRadius(height_ / 2.0f)
                          .backgroundColor(on ? trackOn_ : trackOff_);

        std::vector<Child> row;
        row.emplace_back(std::move(track));
        if (!label_.empty()) {
            row.emplace_back(Gap(6.0f));
            auto txt = Text(label_);
            if (textColor_)
                txt.color(*textColor_);
            if (fontSize_)
                txt.fontSize(*fontSize_);
            row.emplace_back(std::move(txt));
        }

        auto out = Row(std::move(row))
                       .alignItems(AlignItems::Center)
                       .cursor(disabled ? CursorShape::Arrow : CursorShape::Pointer)
                       .focusable(!disabled)
                       .onClick(std::move(click));

        // Keyboard toggle: opt-in, on the FOCUSABLE Row (keydown routes to the
        // focused node and bubbles up). Consumes all keys while focused, so it
        // is attached only when a keycode is set (the Checkbox precedent).
        if (toggleKeyCode_) {
            out.onKeyDown([on, onChange, disabled, key = *toggleKeyCode_](int keyCode, uint16_t, bool) {
                if (keyCode == key && !disabled && onChange)
                    onChange(!on);
            });
        }
        return out;
    }

    bool on_;
    std::function<void(bool)> onChange_;
    std::string label_;
    bool disabled_ = false;
    uint32_t trackOff_ = kSwitchTrackOff;
    uint32_t trackOn_ = kSwitchTrackOn;
    uint32_t chip_ = kSwitchChip;
    float width_ = kSwitchWidth;
    float height_ = kSwitchHeight;
    std::optional<uint32_t> textColor_;
    std::optional<float> fontSize_;
    std::optional<int> toggleKeyCode_;
};

[[nodiscard]] inline SwitchBuilder Switch(bool on) { return SwitchBuilder{on}; }

}  // namespace yui::widgets
