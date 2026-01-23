// Application state for yui showcase
// Centralized reactive state with theme

#pragma once

#include "Theme.hpp"
#include "yui/yui.hpp"

#include <string>
#include <vector>

namespace showcase {

// ============================================================================
// Application State
// ============================================================================

struct AppState {
    // Theme
    Theme theme = Midnight();
    int themeIndex = 0;
    int sizeIndex = 0;  // 0=default, 1=compact, 2=spacious

    // Form demo
    std::string username;
    std::string password;

    // List demo
    std::vector<std::string> items = {"Apple", "Banana", "Cherry"};
    std::string newItemText;

    // Interaction demo
    int clickCount = 0;

    // Keyboard demo
    std::string lastKeyName = "None";
    int lastKeyCode = 0;
    std::string keyModifiers;

    // Status feedback
    std::string statusText = "Ready";
    uint32_t statusColor = 0x22c55eFF;
};

// ============================================================================
// Store Access
// ============================================================================

inline yui::Store<AppState>* storePtr = nullptr;

inline yui::Store<AppState>& state() {
    return *storePtr;
}

inline const Theme& theme() {
    return state().use().theme;
}

// ============================================================================
// Theme Switching
// ============================================================================

inline Theme getBaseTheme(int index) {
    switch (index) {
    case 1:
        return Ember();
    case 2:
        return Violet();
    case 3:
        return Monochrome();
    case 4:
        return Cyberpunk();
    case 5:
        return Forest();
    default:
        return Midnight();
    }
}

inline Theme applySize(Theme t, int sizeIndex) {
    switch (sizeIndex) {
    case 1:
        return withCompactSize(t);
    case 2:
        return withSpaciousSize(t);
    default:
        return t;
    }
}

inline void setTheme(int index) {
    state().set([index](AppState& s) {
        s.themeIndex = index;
        s.theme = applySize(getBaseTheme(index), s.sizeIndex);
    });
}

inline void setSizeModifier(int index) {
    state().set([index](AppState& s) {
        s.sizeIndex = index;
        s.theme = applySize(getBaseTheme(s.themeIndex), index);
    });
}

inline void setStatus(const std::string& text, uint32_t color) {
    state().set([text, color](AppState& s) {
        s.statusText = text;
        s.statusColor = color;
    });
}

}  // namespace showcase
