#pragma once

#include <yui/core/VNode.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace yui::widgets {

// Checkbox: a controlled boolean toggle. The `checked` state is passed in (the
// app owns it via onChange) — the widget holds NO transient state, so the inner
// "check" glyph's presence always mirrors the value handed in this render.
// Clicking fires onChange(!checked); the app flips its own state and re-renders.
//
// The check glyph is an inner filled Box (borderRadius, no unicode Text) so
// there is no font dependency for the mark itself.
//
// Keyboard: opt-in. When toggleKeyCode is set an onKeyDown is attached to the
// focusable Row that fires onChange(!checked) for that keycode — but note this
// handler runs to completion for EVERY key while the checkbox is focused,
// thereby CONSUMING all keys that bubble through it. Leave it unset (default) to
// attach no key handler.

inline constexpr uint32_t kCheckboxBorder = 0x808080FFu;  // box outline
inline constexpr uint32_t kCheckboxCheck = 0x4A90D9FFu;   // inner mark when checked
inline constexpr float kCheckboxSize = 16.0f;

class CheckboxBuilder {
public:
    explicit CheckboxBuilder(bool checked) : checked_(checked) {}

    CheckboxBuilder& onChange(std::function<void(bool)> fn) {
        onChange_ = std::move(fn);
        return *this;
    }
    CheckboxBuilder& label(std::string text) {
        label_ = std::move(text);
        return *this;
    }
    CheckboxBuilder& disabled(bool v = true) {
        disabled_ = v;
        return *this;
    }
    CheckboxBuilder& boxColor(uint32_t c) {
        boxColor_ = c;
        return *this;
    }
    CheckboxBuilder& checkColor(uint32_t c) {
        checkColor_ = c;
        return *this;
    }
    CheckboxBuilder& size(float s) {
        size_ = s;
        return *this;
    }
    CheckboxBuilder& textColor(uint32_t c) {
        textColor_ = c;
        return *this;
    }
    CheckboxBuilder& fontSize(float s) {
        fontSize_ = s;
        return *this;
    }
    // App-supplied keycode that toggles while focused. Unset -> no key handler.
    CheckboxBuilder& toggleKeyCode(int keyCode) {
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
        const bool checked = checked_;
        auto onChange = onChange_;
        const bool disabled = disabled_;

        // Consuming even when disabled: a checkbox click must never bubble into
        // a parent handler (Modal.hpp consuming-no-op precedent).
        auto click = [checked, onChange, disabled] {
            if (!disabled && onChange)
                onChange(!checked);
        };

        VNode box = Box(When(checked, Box()
                                          .width(size_ - 6.0f)
                                          .height(size_ - 6.0f)
                                          .borderRadius(2.0f)
                                          .backgroundColor(checkColor_)
                                          .setKey("check")))
                        .setKey("box")
                        .width(size_)
                        .height(size_)
                        .borderWidth(1.5f)
                        .borderColor(boxColor_)
                        .borderRadius(3.0f)
                        .justifyContent(JustifyContent::Center)
                        .alignItems(AlignItems::Center)
                        .focusStyle(BoxStyle{.borderColor = checkColor_});

        std::vector<Child> row;
        row.emplace_back(std::move(box));
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

        // Keyboard toggle is opt-in and lives on the FOCUSABLE node (the Row):
        // keydown routes to the focused node and bubbles UP, so a handler on an
        // inner box would never be reached. This handler runs to completion for
        // every key while the checkbox is focused — thereby CONSUMING all keys
        // that bubble through it — so it is attached only when a keycode is set.
        if (toggleKeyCode_) {
            out.onKeyDown([checked, onChange, disabled, key = *toggleKeyCode_](int keyCode, uint16_t, bool) {
                if (keyCode == key && !disabled && onChange)
                    onChange(!checked);
            });
        }
        return out;
    }

    bool checked_;
    std::function<void(bool)> onChange_;
    std::string label_;
    bool disabled_ = false;
    uint32_t boxColor_ = kCheckboxBorder;
    uint32_t checkColor_ = kCheckboxCheck;
    float size_ = kCheckboxSize;
    std::optional<uint32_t> textColor_;
    std::optional<float> fontSize_;
    std::optional<int> toggleKeyCode_;
};

[[nodiscard]] inline CheckboxBuilder Checkbox(bool checked) { return CheckboxBuilder{checked}; }

}  // namespace yui::widgets
