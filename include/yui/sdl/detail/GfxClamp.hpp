#pragma once

#include <cmath>
#include <cstdint>

namespace yui {
namespace sdl {
namespace detail {

// SDL2_gfx primitives take Sint16 (int16_t) coordinates, but layout runs in
// float and absolute coordinates can leave that range (tall scrolled content,
// large canvases). A bare static_cast wraps out-of-range values back into view
// as garbage geometry; saturating pins them to the Sint16 boundary instead, so
// oversized geometry draws clamped — far offscreen for any realistic viewport —
// never wrapped. In-range values truncate exactly like the cast they replace.
// NaN maps to 0: there is no position to preserve, only wrapping to avoid.
// (Deliberately SDL-header-free so the arithmetic is unit-testable headlessly.)
inline std::int16_t clampToGfxCoord(float v) {
    if (std::isnan(v))
        return 0;
    if (v <= -32768.0f)
        return -32768;
    if (v >= 32767.0f)
        return 32767;
    return static_cast<std::int16_t>(v);
}

}  // namespace detail
}  // namespace sdl
}  // namespace yui
