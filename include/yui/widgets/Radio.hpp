#pragma once

#include <yui/core/VNode.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace yui::widgets {

// Radio + RadioGroup: controlled single-select. A Radio is one circle whose
// filled inner dot ("dot") is present iff `selected` — controlled, stateless.
// A RadioGroup renders one Radio per option and reports the chosen index via
// onChange; selecting the already-selected index is a guarded no-op.
//
// Ring and dot are borderRadius circles (no unicode glyph, no font dependency).

inline constexpr uint32_t kRadioDot = 0x4A90D9FFu;   // filled dot when selected
inline constexpr uint32_t kRadioRing = 0x808080FFu;  // outer ring
inline constexpr float kRadioSize = 16.0f;

class RadioBuilder {
public:
    explicit RadioBuilder(bool selected) : selected_(selected) {}

    RadioBuilder& onSelect(std::function<void()> fn) {
        onSelect_ = std::move(fn);
        return *this;
    }
    RadioBuilder& disabled(bool v = true) {
        disabled_ = v;
        return *this;
    }
    RadioBuilder& label(std::string text) {
        label_ = std::move(text);
        return *this;
    }
    RadioBuilder& dotColor(uint32_t c) {
        dotColor_ = c;
        return *this;
    }
    RadioBuilder& ringColor(uint32_t c) {
        ringColor_ = c;
        return *this;
    }
    RadioBuilder& size(float s) {
        size_ = s;
        return *this;
    }
    RadioBuilder& textColor(uint32_t c) {
        textColor_ = c;
        return *this;
    }
    RadioBuilder& fontSize(float s) {
        fontSize_ = s;
        return *this;
    }
    // Structural key on the outer Row (RadioGroup keys each option). Stored as
    // the string key so tests can find an option by its index via findByKey.
    RadioBuilder& setKey(std::int64_t k) {
        key_ = std::to_string(k);
        return *this;
    }
    RadioBuilder& setKey(std::string k) {
        key_ = std::move(k);
        return *this;
    }

    // --- Conversion seam ---
    operator VNode() const& { return build(); }
    operator VNode() && { return build(); }
    operator Child() const& { return Child{build()}; }
    operator Child() && { return Child{build()}; }

private:
    VNode build() const {
        auto onSelect = onSelect_;
        const bool disabled = disabled_;

        // Consuming even when disabled (Modal consuming-no-op precedent).
        auto click = [onSelect, disabled] {
            if (!disabled && onSelect)
                onSelect();
        };

        VNode circle = Box(When(selected_, Box()
                                               .width(size_ - 8.0f)
                                               .height(size_ - 8.0f)
                                               .borderRadius((size_ - 8.0f) / 2.0f)
                                               .backgroundColor(dotColor_)
                                               .setKey("dot")))
                           .width(size_)
                           .height(size_)
                           .borderRadius(size_ / 2.0f)
                           .borderWidth(1.5f)
                           .borderColor(ringColor_)
                           .justifyContent(JustifyContent::Center)
                           .alignItems(AlignItems::Center);

        std::vector<Child> row;
        row.emplace_back(std::move(circle));
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
        if (key_)
            out.setKey(*key_);
        return out;
    }

    bool selected_;
    std::function<void()> onSelect_;
    bool disabled_ = false;
    std::string label_;
    uint32_t dotColor_ = kRadioDot;
    uint32_t ringColor_ = kRadioRing;
    float size_ = kRadioSize;
    std::optional<uint32_t> textColor_;
    std::optional<float> fontSize_;
    std::optional<std::string> key_;
};

[[nodiscard]] inline RadioBuilder Radio(bool selected) { return RadioBuilder{selected}; }

// RadioGroup: a Box laying out one Radio per option, tracking a single selected
// index. onChange(i) fires only when i differs from the current value.
class RadioGroupBuilder {
public:
    explicit RadioGroupBuilder(std::vector<std::string> options) : options_(std::move(options)) {}

    RadioGroupBuilder& value(int v) {
        value_ = v;
        return *this;
    }
    RadioGroupBuilder& onChange(std::function<void(int)> fn) {
        onChange_ = std::move(fn);
        return *this;
    }
    RadioGroupBuilder& direction(FlexDirection d) {
        direction_ = d;
        return *this;
    }
    RadioGroupBuilder& gap(float g) {
        gap_ = g;
        return *this;
    }
    RadioGroupBuilder& disabled(bool v = true) {
        disabled_ = v;
        return *this;
    }
    // Pass-through visual setters (mirror Radio).
    RadioGroupBuilder& dotColor(uint32_t c) {
        dotColor_ = c;
        return *this;
    }
    RadioGroupBuilder& ringColor(uint32_t c) {
        ringColor_ = c;
        return *this;
    }
    RadioGroupBuilder& size(float s) {
        size_ = s;
        return *this;
    }
    RadioGroupBuilder& textColor(uint32_t c) {
        textColor_ = c;
        return *this;
    }
    RadioGroupBuilder& fontSize(float s) {
        fontSize_ = s;
        return *this;
    }

    // --- Conversion seam ---
    operator VNode() const& { return build(); }
    operator VNode() && { return build(); }
    operator Child() const& { return Child{build()}; }
    operator Child() && { return Child{build()}; }

private:
    VNode build() const {
        const int value = value_;
        auto onChange = onChange_;

        std::vector<Child> kids;
        kids.reserve(options_.size());
        for (int i = 0; i < static_cast<int>(options_.size()); ++i) {
            auto radio = Radio(i == value).label(options_[i]).disabled(disabled_).dotColor(dotColor_).ringColor(ringColor_).size(size_);
            radio.setKey(i);
            if (textColor_)
                radio.textColor(*textColor_);
            if (fontSize_)
                radio.fontSize(*fontSize_);
            radio.onSelect([onChange, i, value] {
                if (i != value && onChange)
                    onChange(i);
            });
            kids.emplace_back(std::move(radio));
        }

        return Box(std::move(kids)).flexDirection(direction_).gap(gap_);
    }

    std::vector<std::string> options_;
    int value_ = -1;
    std::function<void(int)> onChange_;
    FlexDirection direction_ = FlexDirection::Column;
    float gap_ = 6.0f;
    bool disabled_ = false;
    uint32_t dotColor_ = kRadioDot;
    uint32_t ringColor_ = kRadioRing;
    float size_ = kRadioSize;
    std::optional<uint32_t> textColor_;
    std::optional<float> fontSize_;
};

[[nodiscard]] inline RadioGroupBuilder RadioGroup(std::vector<std::string> options) {
    return RadioGroupBuilder{std::move(options)};
}

}  // namespace yui::widgets
