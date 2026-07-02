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

// Payload handed to onDrag: the pointer's current position, the delta from the
// previous captured move, and the press anchor the drag started from. All
// coordinates are absolute window coordinates.
struct DragEvent {
    float x, y;            // current pointer position
    float dx, dy;          // delta from the previous captured move
    float startX, startY;  // the press anchor that started the drag
    MouseButton button;    // the button held throughout the drag
};

// Event types
struct Event {
    enum class Type {
        MouseDown,
        MouseUp,
        MouseMove,
        MouseEnter,
        MouseLeave,
        Drag,
        Scroll,
        KeyDown,
        KeyUp,
    };

    Type type;
    float x = 0;  // Absolute window coordinates (dispatch writes the raw
    float y = 0;  // platform coords through unchanged — never node-relative)
    MouseButton button = MouseButton::Left;

    // Scroll delta (for Scroll events)
    float scrollDeltaX = 0;
    float scrollDeltaY = 0;

    // Drag payload (for Drag events): the press anchor the drag started from
    // and the delta from the previous captured move.
    float dragStartX = 0;
    float dragStartY = 0;
    float dragDeltaX = 0;
    float dragDeltaY = 0;

    // Position in the multi-click chain at this press/release (1 = single,
    // 2 = double, ...). Carried on MouseDown/MouseUp; onDoubleClick fires at 2.
    // The counter keeps climbing (3+ is carried for future triple-click
    // consumers; there is no public onTripleClick yet).
    int clickCount = 1;

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

    // For MouseUp only: the node under the pointer at RELEASE (hit-tested), or
    // null when the release lands outside the window/tree. During capture the
    // dispatch target is the CAPTOR, so this is the only record of where the
    // release actually landed; the click gate additionally requires it to be
    // within the handler node's subtree (symmetric with pressedTarget).
    Node* releaseTarget = nullptr;

    void consume() { consumed = true; }
};

}  // namespace yui
