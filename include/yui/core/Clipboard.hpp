#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace yui {

class Host;

// Platform clipboard seam. A host installs one via Host::setClipboard; it is
// handed to the event handler PER-CALL for Cut/Copy/Paste (see
// EventHandler::handleEditCommand) and never stored below Host, so all lifetime
// management lives on the Host/IClipboard link.
//
// Lifetime is self-managed in BOTH directions, mirroring the ITextMeasurer host
// link (see Measure.hpp) — either object may die first:
//   - When the clipboard is destroyed, ~IClipboard nulls the clipboard_ of
//     every host it is still installed on, so a dead clipboard can never be
//     handed to an edit command. (clipboard-dies-first)
//   - When a host is destroyed, ~Host deregisters from its clipboard, so the
//     clipboard's destructor never touches that host's freed storage.
//     (host-dies-first)
// The link uses the same liveness-token discipline as Fiber/Store/isHostLive:
// each registration carries a copy of the host's `alive` token and is acted on
// only while that token reports true.
struct IClipboard {
    virtual ~IClipboard() {
        // clipboard-dies-first: clear every host that still points at us.
        for (auto& reg : registrations_) {
            if (reg.hostAlive && *reg.hostAlive) {
                reg.clearHost();
            }
        }
    }

    // Current clipboard text. Empty when the platform clipboard is empty or
    // holds non-text content.
    virtual std::string getText() = 0;

    // Replace the clipboard contents with `text`.
    virtual void setText(const std::string& text) = 0;

private:
    friend class Host;

    // A host this clipboard is currently installed on. `clearHost` nulls that
    // host's clipboard_; it is only invoked while `hostAlive` reports the host
    // is alive.
    struct HostRegistration {
        const Host* host = nullptr;
        std::shared_ptr<bool> hostAlive;
        std::function<void()> clearHost;
    };

    // Called by Host::setClipboard when this clipboard is installed on a host.
    void attachHost(const Host* host,
                    std::shared_ptr<bool> hostAlive,
                    std::function<void()> clearHost) {
        registrations_.push_back({host, std::move(hostAlive), std::move(clearHost)});
    }

    // Called by Host::setClipboard (on replacement) and by ~Host, so the
    // clipboard no longer holds a stale record for a host that has moved on or
    // been destroyed.
    void detachHost(const Host* host) {
        std::erase_if(registrations_, [host](const HostRegistration& r) { return r.host == host; });
    }

    std::vector<HostRegistration> registrations_;
};

}  // namespace yui
