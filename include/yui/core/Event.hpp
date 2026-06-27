#pragma once

#include <cstdint>

namespace yui {

class Node;

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

    // For MouseUp only: the node that received the matching MouseDown (the press
    // target), or null. A click fires only on the handler node that ALSO received
    // the press — so a release whose press landed elsewhere (e.g. an orphan
    // release from opening an overlay under the cursor) does not fire onClick.
    // Compared against the handler-bearing node in the bubble walk, so a press and
    // release that resolve to the same onClick ancestor still count as a click.
    Node* pressedTarget = nullptr;

    void consume() { consumed = true; }
};

}  // namespace yui
