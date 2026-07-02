#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yui {

class Host;

// Size returned by measure functions.
struct Size {
    float width = 0;
    float height = 0;
};

// Vertical font metrics for one (face, size). All values are in pixels;
// descent is positive (distance below the baseline), unlike the negative
// convention some backends report natively.
struct FontMetrics {
    float ascent;      // baseline -> top of line box
    float descent;     // baseline -> bottom of line box
    float lineHeight;  // baseline-to-baseline advance
};

// Rough per-run advance estimate used when no backend measurer is available:
// ~0.6 * fontSize per character (a byte-count estimate). Font-agnostic — the
// name parameter exists only to keep one uniform measure signature.
inline float fallbackMeasureRun(std::string_view run, float fontSize, std::string_view font = {}) {
    (void)font;
    return static_cast<float>(run.size()) * fontSize * 0.6f;
}

// Heuristic vertical metrics matching fallbackMeasure's height convention
// (lineHeight == fontSize), split 80/20 across the baseline.
inline FontMetrics fallbackFontMetrics(float fontSize, std::string_view font = {}) {
    (void)font;
    return {0.8f * fontSize, 0.2f * fontSize, fontSize};
}

// Rough text size estimate used when no backend measurer is available: the
// shared wrap algorithm (render::wrapText) over fallbackMeasureRun /
// fallbackFontMetrics, so the fallback wraps exactly like a real measurer.
// maxWidth (0 = no limit) is the wrapping constraint. Defined out-of-line in
// src/render/Measure.cpp.
Size fallbackMeasure(const std::string& text, float fontSize, float maxWidth, const std::string& font = {});

// Backend-provided text measurement. A host installs one of these via
// Host::setTextMeasurer; it is reached from a node's measure callback through
// the per-host Yoga config context, so measurement is scoped per host with no
// global mutable state.
//
// Lifetime is self-managed in BOTH directions, so either object may die first:
//   - When the measurer is destroyed, ~ITextMeasurer clears the config context
//     of every host it is still installed on, so a dead measurer can never be
//     read from a relayout. (measurer-dies-first)
//   - When a host is destroyed, ~Host deregisters from its measurer, so the
//     measurer's destructor never touches that host's freed config. (host-dies-
//     first)
// The link uses the same liveness-token discipline as Fiber/Store/isHostLive:
// each registration carries a copy of the host's `alive` token and is acted on
// only while that token reports true.
struct ITextMeasurer {
    virtual ~ITextMeasurer() {
        // measurer-dies-first: clear every host that still points at us.
        for (auto& reg : registrations_) {
            if (reg.hostAlive && *reg.hostAlive) {
                reg.clearContext();
            }
        }
    }

    // Measure text at the given font size. maxWidth (0 = no limit) is the
    // wrapping constraint from layout. `font` names a registered font face (empty
    // ⇒ the host/renderer default); it MUST be honored so a node's measured size
    // matches its drawn size (a glyph measured in the wrong face mis-sizes the
    // layout).
    //
    // NON-virtual: there is exactly one sizing rule — the shared wrap algorithm
    // (render::wrapText) over the measureRun/fontMetrics primitives below — so
    // measure and paint agree by construction. Width is the widest wrapped run;
    // height is (#runs * lineHeight). Defined out-of-line in
    // src/render/Measure.cpp.
    Size measure(const std::string& text, float fontSize, float maxWidth, const std::string& font) const;

    // Advance width of ONE run — no wrapping, and `run` must not contain
    // newlines. This is the primitive the shared wrapping layer is built on;
    // it must agree with drawTextRun so a run draws at the width it measured.
    virtual float measureRun(std::string_view run, float fontSize, std::string_view font) const = 0;

    // Vertical metrics for (face, size). Line boxes and baselines are derived
    // from these, never from per-string ink bounds (which vary with content).
    virtual FontMetrics fontMetrics(float fontSize, std::string_view font) const = 0;

private:
    friend class Host;

    // A host this measurer is currently installed on. `clearContext` resets that
    // host's yoga config context to null; it is only invoked while `hostAlive`
    // reports the host is alive.
    struct HostRegistration {
        const Host* host = nullptr;
        std::shared_ptr<bool> hostAlive;
        std::function<void()> clearContext;
    };

    // Called by Host::setTextMeasurer when this measurer is installed on a host.
    void attachHost(const Host* host,
                    std::shared_ptr<bool> hostAlive,
                    std::function<void()> clearContext) {
        registrations_.push_back({host, std::move(hostAlive), std::move(clearContext)});
    }

    // Called by Host::setTextMeasurer (on replacement) and by ~Host, so the
    // measurer no longer holds a stale record for a host that has moved on or
    // been destroyed.
    void detachHost(const Host* host) {
        std::erase_if(registrations_, [host](const HostRegistration& r) { return r.host == host; });
    }

    std::vector<HostRegistration> registrations_;
};

}  // namespace yui
