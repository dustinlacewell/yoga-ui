// Main showcase application
// Assembles features into complete UI

#pragma once

#include "Features.hpp"

namespace showcase {

using namespace yui;

// ============================================================================
// Header Bar
// ============================================================================

inline VNode Header(const std::string& title) {
    auto& s = state().use();
    const auto& t = s.theme;

    return Row({
                   Text_(title, t.text, 18.0f),
                   Spacer(),
                   StatusBadge(s.statusText, s.statusColor),
               })
        .padding(t.padding)
        .alignItems(AlignItems::Center)
        .backgroundColor(t.surface);
}

// ============================================================================
// Footer
// ============================================================================

inline VNode Footer(const std::string& backend) {
    const auto& t = theme();

    return Box(Text_("yui showcase | " + backend, t.textMuted, 11.0f))
        .padding(8)
        .justifyContent(JustifyContent::Center)
        .backgroundColor(t.surface);
}

// ============================================================================
// Main Application
// ============================================================================

inline VNode App(const std::string& title, const std::string& backend, VNode canvasDemo = Box()) {
    const auto& t = theme();

    return Column({
                      Header(title),

                      // Theme switcher bar
                      Box({ThemeSwitcher()}).padding(t.padding).backgroundColor(t.bg),

                      // Main content
                      Scroll(Row({
                                     // Left column
                                     Column({
                                                LoginForm(),
                                                Counter(),
                                                KeyboardDemo(),
                                            })
                                         .gap(t.gap)
                                         .flexBasis(0)
                                         .flexGrow(1),

                                     // Right column
                                     Column({
                                                DynamicList(),
                                                LayoutDemo(),
                                            })
                                         .gap(t.gap)
                                         .flexBasis(0)
                                         .flexGrow(1),

                                     // Third column (optional canvas demo)
                                     Column({
                                                ScrollDemo(),
                                                std::move(canvasDemo),
                                            })
                                         .gap(t.gap)
                                         .flexBasis(0)
                                         .flexGrow(1),
                                 })
                                 .gap(t.gap)
                                 .padding(t.padding))
                          .flexGrow(1),

                      Footer(backend),
                  })
        .backgroundColor(t.bg)
        .onKeyDown([](int keyCode, uint16_t mods) { handleKeyboardEvent(keyCode, mods); });
}

}  // namespace showcase
