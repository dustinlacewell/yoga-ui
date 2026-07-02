#pragma once

#include <cstdint>

namespace yui {

// Single source of truth for visual defaults that BOTH renderer backends (SDL and
// NanoVG) and the core text-measure fallback must agree on. Previously these
// constants were duplicated per backend and had drifted apart, producing two
// classes of bug:
//
//   1. Correctness: the measure-func fallback assumed a 12px font while the SDL
//      backend painted text at 14px, so layout reserved less space than paint
//      consumed and text clipped. The fallback (Node.cpp measureFunc) and the
//      paint size MUST read the same constant — see kDefaultFontSize below.
//   2. Cosmetic parity: Input default colors / radius / padding differed between
//      backends, so the same tree looked different depending on the host.
//
// Anything a renderer would otherwise hardcode as a "default style" and that the
// other backend (or layout) should match belongs here, not in a backend .cpp.
namespace render_defaults {

// --- Text ---

// Default font size used when a Text/Input node leaves fontSize unset. This is
// the load-bearing constant: the measure fallback (src/core/Node.cpp measureFunc)
// and every backend's text paint path resolve fontSize against THIS value, so
// layout and paint agree. Value chosen to match the SDL backend's historical
// paint size (14); NanoVG previously defaulted to 12, which is the value being
// reconciled away.
inline constexpr float kDefaultFontSize = 14.0f;

// Default text color (0xRRGGBBAA): opaque white.
inline constexpr uint32_t kDefaultTextColor = 0xFFFFFFFFu;

// Placeholder text color for an empty Input (0xRRGGBBAA).
inline constexpr uint32_t kPlaceholderColor = 0x808080FFu;

// --- Input box chrome ---

inline constexpr uint32_t kInputBg = 0x282828FFu;
inline constexpr uint32_t kInputBorder = 0x505050FFu;
inline constexpr uint32_t kInputHoverBorder = 0x707070FFu;
inline constexpr uint32_t kInputFocusBorder = 0x4A9FFFFFu;

inline constexpr float kInputBorderWidth = 1.0f;
inline constexpr float kInputBorderRadius = 4.0f;

// Horizontal inset from the Input's left edge to its text/caret. Shared so the
// caret position computed by each backend lands at the same x.
inline constexpr float kInputTextPad = 8.0f;

// --- Focus caret ---

// Vertical inset of the blinking caret from the Input's top/bottom edges.
inline constexpr float kCaretInset = 3.0f;

// Stroke width of the blinking caret.
inline constexpr float kCaretWidth = 1.0f;

// Caret blink period (ms) and the on-fraction of that period: phase below
// kOnMs => visible. Advanced per-frame by InputNode::updateBlink while
// focused. Matches the original NanoVG wall-clock cadence.
inline constexpr long kCaretBlinkPeriodMs = 1000;
inline constexpr long kCaretBlinkOnMs = 530;

// --- Pointer interaction ---

// Chebyshev distance (px) the pointer must move from the press anchor before
// the press becomes a drag: within this ring a release is a click; beyond it
// Drag events stream and the release fires no click.
inline constexpr float kDragThresholdPx = 4.0f;

// Multi-click chaining: a press within this interval AND radius of the previous
// click (same button) increments Event::clickCount (2 = double-click). The
// clock is EventHandler::advanceClock's dt accumulation, not a wall clock.
inline constexpr double kMultiClickIntervalMs = 500.0;
inline constexpr float kMultiClickRadiusPx = 4.0f;

}  // namespace render_defaults
}  // namespace yui
