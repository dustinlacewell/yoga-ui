// Theme system for yui showcase
// Reactive theme with presets for colors, radii, and spacing

#pragma once

#include <cstdint>

namespace showcase {

// ============================================================================
// Theme Configuration
// ============================================================================

struct Theme {
    // Surface colors (layered from back to front)
    uint32_t bg;
    uint32_t surface;
    uint32_t surfaceAlt;
    uint32_t border;

    // Text colors
    uint32_t text;
    uint32_t textMuted;
    uint32_t textOnAccent;  // For text on accent-colored backgrounds

    // Accent & semantic colors
    uint32_t accent;
    uint32_t accentHover;
    uint32_t success;
    uint32_t warning;
    uint32_t danger;

    // Geometry
    float radiusSm;
    float radiusMd;
    float radiusLg;
    float gap;
    float padding;
};

// ============================================================================
// Default geometry (shared by color themes)
// ============================================================================

constexpr float DEFAULT_RADIUS_SM = 4.0f;
constexpr float DEFAULT_RADIUS_MD = 6.0f;
constexpr float DEFAULT_RADIUS_LG = 10.0f;
constexpr float DEFAULT_GAP = 8.0f;
constexpr float DEFAULT_PADDING = 12.0f;

// ============================================================================
// Color Presets
// ============================================================================

// Midnight: Deep blue, muted accent buttons that brighten on hover
inline Theme Midnight() {
    return {
        .bg = 0x0a0c10FF,
        .surface = 0x12161cFF,
        .surfaceAlt = 0x1e2530FF,
        .border = 0x2e3a48FF,
        .text = 0xe8ecf0FF,
        .textMuted = 0x6b7a8aFF,
        .textOnAccent = 0xffffffFF,
        .accent = 0x2563ebFF,       // Deep blue - readable with white text
        .accentHover = 0x3b82f6FF,  // Brighter on hover
        .success = 0x16a34aFF,      // Deep green
        .warning = 0xd97706FF,      // Deep amber
        .danger = 0xdc2626FF,       // Deep red
        .radiusSm = DEFAULT_RADIUS_SM,
        .radiusMd = DEFAULT_RADIUS_MD,
        .radiusLg = DEFAULT_RADIUS_LG,
        .gap = DEFAULT_GAP,
        .padding = DEFAULT_PADDING,
    };
}

// Ember: Warm tones, orange accent
inline Theme Ember() {
    return {
        .bg = 0x0c0908FF,
        .surface = 0x1a1412FF,
        .surfaceAlt = 0x2a201cFF,
        .border = 0x3d322cFF,
        .text = 0xf0e8e4FF,
        .textMuted = 0x8a7a70FF,
        .textOnAccent = 0x000000FF,  // Black on orange
        .accent = 0xea580cFF,        // Deep orange
        .accentHover = 0xf97316FF,   // Brighter orange
        .success = 0x16a34aFF,
        .warning = 0xca8a04FF,
        .danger = 0xdc2626FF,
        .radiusSm = DEFAULT_RADIUS_SM,
        .radiusMd = DEFAULT_RADIUS_MD,
        .radiusLg = DEFAULT_RADIUS_LG,
        .gap = DEFAULT_GAP,
        .padding = DEFAULT_PADDING,
    };
}

// Violet: Purple accent
inline Theme Violet() {
    return {
        .bg = 0x0c0a10FF,
        .surface = 0x16121cFF,
        .surfaceAlt = 0x221c2aFF,
        .border = 0x362e40FF,
        .text = 0xece8f0FF,
        .textMuted = 0x8878a0FF,
        .textOnAccent = 0xffffffFF,
        .accent = 0x7c3aedFF,       // Deep purple
        .accentHover = 0x8b5cf6FF,  // Brighter purple
        .success = 0x16a34aFF,
        .warning = 0xd97706FF,
        .danger = 0xdc2626FF,
        .radiusSm = DEFAULT_RADIUS_SM,
        .radiusMd = DEFAULT_RADIUS_MD,
        .radiusLg = DEFAULT_RADIUS_LG,
        .gap = DEFAULT_GAP,
        .padding = DEFAULT_PADDING,
    };
}

// Monochrome: True grayscale - NO color anywhere
inline Theme Monochrome() {
    return {
        .bg = 0x0a0a0aFF,
        .surface = 0x171717FF,
        .surfaceAlt = 0x262626FF,
        .border = 0x404040FF,
        .text = 0xe5e5e5FF,
        .textMuted = 0x737373FF,
        .textOnAccent = 0x000000FF,  // Black on light gray
        .accent = 0xa3a3a3FF,        // Light gray accent
        .accentHover = 0xd4d4d4FF,   // Lighter on hover
        .success = 0xc0c0c0FF,       // Gray "success"
        .warning = 0x909090FF,       // Gray "warning"
        .danger = 0x707070FF,        // Gray "danger"
        .radiusSm = DEFAULT_RADIUS_SM,
        .radiusMd = DEFAULT_RADIUS_MD,
        .radiusLg = DEFAULT_RADIUS_LG,
        .gap = DEFAULT_GAP,
        .padding = DEFAULT_PADDING,
    };
}

// Cyberpunk: Cyan neon on black
inline Theme Cyberpunk() {
    return {
        .bg = 0x030306FF,
        .surface = 0x0a0a10FF,
        .surfaceAlt = 0x14141cFF,
        .border = 0x202030FF,
        .text = 0xf0f0ffFF,
        .textMuted = 0x606080FF,
        .textOnAccent = 0x000000FF,  // Black on cyan
        .accent = 0x06b6d4FF,        // Cyan
        .accentHover = 0x22d3eeFF,   // Brighter cyan
        .success = 0x10b981FF,
        .warning = 0xf59e0bFF,
        .danger = 0xec4899FF,  // Pink for danger
        .radiusSm = DEFAULT_RADIUS_SM,
        .radiusMd = DEFAULT_RADIUS_MD,
        .radiusLg = DEFAULT_RADIUS_LG,
        .gap = DEFAULT_GAP,
        .padding = DEFAULT_PADDING,
    };
}

// Forest: Green accent
inline Theme Forest() {
    return {
        .bg = 0x080a08FF,
        .surface = 0x101610FF,
        .surfaceAlt = 0x1a221aFF,
        .border = 0x2a382aFF,
        .text = 0xe8f0e8FF,
        .textMuted = 0x688068FF,
        .textOnAccent = 0x000000FF,  // Black on green
        .accent = 0x22c55eFF,        // Green
        .accentHover = 0x4ade80FF,   // Brighter green
        .success = 0x34d399FF,
        .warning = 0xfbbf24FF,
        .danger = 0xf87171FF,
        .radiusSm = DEFAULT_RADIUS_SM,
        .radiusMd = DEFAULT_RADIUS_MD,
        .radiusLg = DEFAULT_RADIUS_LG,
        .gap = DEFAULT_GAP,
        .padding = DEFAULT_PADDING,
    };
}

// ============================================================================
// Size Modifiers (apply to any theme)
// ============================================================================

inline Theme withCompactSize(Theme t) {
    t.radiusSm = 2.0f;
    t.radiusMd = 4.0f;
    t.radiusLg = 6.0f;
    t.gap = 4.0f;
    t.padding = 8.0f;
    return t;
}

inline Theme withSpaciousSize(Theme t) {
    t.radiusSm = 6.0f;
    t.radiusMd = 10.0f;
    t.radiusLg = 16.0f;
    t.gap = 12.0f;
    t.padding = 18.0f;
    return t;
}

}  // namespace showcase
