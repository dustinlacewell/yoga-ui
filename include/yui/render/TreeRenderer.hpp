#pragma once

#include "../core/ErrorHandler.hpp"
#include "Backend.hpp"

namespace yui {
class Node;
}

namespace yui::render {

// The backend-neutral render walk: descends the rendered Node tree in absolute
// coordinates and draws every primitive through the IRenderBackend surface —
// style cascade, scroll clip/offset math, input chrome, and caret included.
// Callers bracket it with backend.beginFrame()/endFrame().
//
// noexcept backstop: rendering runs inside the platform's draw callback (a C
// boundary), so a throw must never escape; anything thrown mid-walk is routed
// to onError and endFrame() restores backend state (Canvas draw callbacks are
// additionally isolated per-node inside IRenderBackend::drawCanvas).
void renderTree(Node* root, IRenderBackend& backend, const ErrorHandler& onError) noexcept;

}  // namespace yui::render
