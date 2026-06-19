#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace yui {

class Host;

// Size returned by measure functions.
struct Size {
    float width = 0;
    float height = 0;
};

// Rough text size estimate used when no backend measurer is available.
// Assumes ~0.6 * fontSize per character for width, fontSize for height.
// maxWidth (0 = no limit) clamps the reported width for wrapping.
inline Size fallbackMeasure(const std::string& text, float fontSize, float maxWidth) {
    float charWidth = fontSize * 0.6f;
    float width = static_cast<float>(text.length()) * charWidth;
    if (maxWidth > 0 && width > maxWidth) {
        width = maxWidth;
    }
    return {width, fontSize};
}

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
    // wrapping constraint from layout.
    virtual Size measure(const std::string& text, float fontSize, float maxWidth) const = 0;

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
