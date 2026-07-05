#pragma once

#include <yui/core/VNode.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>

namespace yui::widgets {

// Progress: a purely visual determinate bar — a fill Box laid inside a track
// Box, sized by width-percent. Stateless, event-free: the value is controlled
// (the app owns it), there is nothing to click, and clicks pass straight
// through (no consuming handler) since a progress bar is not interactive.
//
// The glyph is geometry, not text: the fill's width IS the value, so a test
// reads progress by comparing the "fill" node's absolute width against the
// "track" node's — no font dependency.

inline constexpr uint32_t kProgressTrack = 0x2A2A2AFFu;  // groove
inline constexpr uint32_t kProgressFill = 0x4A90D9FFu;   // filled portion
inline constexpr float kProgressHeight = 8.0f;

class ProgressBuilder {
public:
    explicit ProgressBuilder(float value) : value_(value) {}

    ProgressBuilder& trackColor(uint32_t c) {
        trackColor_ = c;
        return *this;
    }
    ProgressBuilder& fillColor(uint32_t c) {
        fillColor_ = c;
        return *this;
    }
    ProgressBuilder& height(float h) {
        height_ = h;
        return *this;
    }
    // Corner radius of both track and fill. Unset -> a full pill (height/2).
    ProgressBuilder& borderRadius(float r) {
        borderRadius_ = r;
        return *this;
    }

    // --- Conversion seam (mirrors BuilderBase) ---
    operator VNode() const& { return build(); }
    operator VNode() && { return build(); }
    operator Child() const& { return Child{build()}; }
    operator Child() && { return Child{build()}; }

private:
    VNode build() const {
        const float clamped = std::clamp(value_, 0.0f, 1.0f);
        const float r = borderRadius_.value_or(height_ / 2.0f);

        VNode fill = Box()
                         .setKey("fill")
                         .widthPercent(clamped * 100.0f)
                         .heightPercent(100.0f)
                         .backgroundColor(fillColor_)
                         .borderRadius(r);

        return Box(std::move(fill))
            .setKey("track")
            .height(height_)
            .widthPercent(100.0f)
            .backgroundColor(trackColor_)
            .borderRadius(r);
    }

    float value_;
    uint32_t trackColor_ = kProgressTrack;
    uint32_t fillColor_ = kProgressFill;
    float height_ = kProgressHeight;
    std::optional<float> borderRadius_;
};

// Factory: single value, no children (a progress bar has no slot).
[[nodiscard]] inline ProgressBuilder Progress(float value) { return ProgressBuilder{value}; }

}  // namespace yui::widgets
