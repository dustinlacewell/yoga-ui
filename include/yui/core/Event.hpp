#pragma once

#include <cstdint>

namespace yui {

// Mouse button
enum class MouseButton { Left, Right, Middle };

// Keyboard modifier flags (matches SDL/GLFW conventions)
enum KeyMod : uint16_t {
    KeyMod_None = 0,
    KeyMod_Shift = 1 << 0,
    KeyMod_Ctrl = 1 << 1,
    KeyMod_Alt = 1 << 2,
    KeyMod_Super = 1 << 3,  // Cmd on macOS, Win key on Windows
    KeyMod_CapsLock = 1 << 4,
    KeyMod_NumLock = 1 << 5,
};

// Event types
struct Event {
    enum class Type {
        MouseDown,
        MouseUp,
        MouseMove,
        MouseEnter,
        MouseLeave,
        Scroll,
        KeyDown,
        KeyUp,
    };

    Type type;
    float x = 0;  // Position relative to receiving node
    float y = 0;
    MouseButton button = MouseButton::Left;

    // Scroll delta (for Scroll events)
    float scrollDeltaX = 0;
    float scrollDeltaY = 0;

    // Keyboard event data (for KeyDown/KeyUp events)
    int keyCode = 0;        // Platform key code (SDL_Keycode or GLFW key)
    uint16_t keyMod = 0;    // Modifier flags (KeyMod bitmask)
    bool keyRepeat = false; // True if this is a key repeat event

    bool consumed = false;  // Set to stop propagation

    void consume() { consumed = true; }
};

}  // namespace yui
