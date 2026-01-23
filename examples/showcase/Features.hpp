// Feature sections for yui showcase
// Composed from primitives, demonstrate specific yui capabilities

#pragma once

#include "Primitives.hpp"
#include "State.hpp"

namespace showcase {

using namespace yui;

// ============================================================================
// Theme Switcher
// ============================================================================

inline VNode ThemeSwitcher() {
    auto& s = state().use();
    const auto& t = s.theme;

    auto themeButton = [&](const std::string& name, int index) {
        bool active = s.themeIndex == index;
        return Box(Text_(name, active ? t.textOnAccent : t.textMuted, 12.0f))
            .backgroundColor(active ? t.accent : t.surfaceAlt)
            .borderRadius(t.radiusSm)
            .paddingTop(6)
            .paddingBottom(6)
            .paddingLeft(10)
            .paddingRight(10)
            .onClick([index] { setTheme(index); })
            .hoverStyle(BoxStyle{.backgroundColor = active ? t.accentHover : t.border});
    };

    auto sizeButton = [&](const std::string& name, int index) {
        bool active = s.sizeIndex == index;
        return Box(Text_(name, active ? t.text : t.textMuted, 11.0f))
            .backgroundColor(active ? t.border : t.surfaceAlt)
            .borderRadius(t.radiusSm)
            .paddingTop(4)
            .paddingBottom(4)
            .paddingLeft(8)
            .paddingRight(8)
            .onClick([index] { setSizeModifier(index); })
            .hoverStyle(BoxStyle{.backgroundColor = t.border});
    };

    return Column({
                      Row({
                              themeButton("Midnight", 0),
                              themeButton("Ember", 1),
                              themeButton("Violet", 2),
                              themeButton("Mono", 3),
                              themeButton("Cyber", 4),
                              themeButton("Forest", 5),
                          })
                          .gap(t.gap)
                          .flexWrap(FlexWrap::Wrap),
                      Row({
                              Label("Size:"),
                              sizeButton("Default", 0),
                              sizeButton("Compact", 1),
                              sizeButton("Spacious", 2),
                          })
                          .gap(t.gap)
                          .alignItems(AlignItems::Center),
                  })
        .gap(t.gap);
}

// ============================================================================
// Login Form - Forms, validation, inputs
// ============================================================================

inline VNode LoginForm() {
    auto& s = state().use();
    const auto& t = s.theme;

    return Section("Login", {
                                LabeledInput("Username", const_cast<std::string*>(&s.username), "Enter username"),
                                LabeledInput("Password", const_cast<std::string*>(&s.password), "Password", true),

                                Row({
                                        Button(
                                            "Sign In",
                                            [] {
                                                auto& s = state().use();
                                                if (!s.username.empty() && !s.password.empty()) {
                                                    setStatus("Welcome, " + s.username, theme().success);
                                                } else {
                                                    setStatus("Fill all fields", theme().warning);
                                                }
                                            },
                                            t.success),
                                        Button(
                                            "Clear",
                                            [] {
                                                state().set([](AppState& s) {
                                                    s.username.clear();
                                                    s.password.clear();
                                                });
                                                setStatus("Cleared", theme().textMuted);
                                            },
                                            t.surfaceAlt),
                                    })
                                    .gap(t.gap),
                            });
}

// ============================================================================
// Dynamic List - Add/remove, keyed lists
// ============================================================================

inline VNode ListItem(const std::string& text, size_t index) {
    const auto& t = theme();

    return Box({
                   Row({
                           Body(text),
                           Spacer(),
                           IconButton("x",
                                      [index] {
                                          state().set([index](AppState& s) {
                                              if (index < s.items.size()) {
                                                  std::string removed = s.items[index];
                                                  s.items.erase(s.items.begin() + index);
                                                  s.statusText = "Removed " + removed;
                                                  s.statusColor = s.theme.warning;
                                              }
                                          });
                                      }),
                       })
                       .alignItems(AlignItems::Center),
               })
        .backgroundColor(t.surfaceAlt)
        .borderRadius(t.radiusSm)
        .padding(t.gap)
        .hoverStyle(BoxStyle{.backgroundColor = t.border})
        .setKey("item-" + std::to_string(index));
}

inline VNode DynamicList() {
    auto& s = state().use();
    const auto& t = s.theme;

    std::vector<VNode> items;
    for (size_t i = 0; i < s.items.size(); i++) {
        items.push_back(ListItem(s.items[i], i));
    }

    return Section(
        "List", {
                    Row({
                            TextInput(const_cast<std::string*>(&s.newItemText), "New item...").flexGrow(1),
                            Button(
                                "+",
                                [] {
                                    state().set([](AppState& s) {
                                        if (!s.newItemText.empty()) {
                                            s.items.push_back(s.newItemText);
                                            s.statusText = "Added " + s.newItemText;
                                            s.statusColor = s.theme.success;
                                            s.newItemText.clear();
                                        }
                                    });
                                },
                                t.success),
                        })
                        .gap(t.gap)
                        .alignItems(AlignItems::Center),

                    Scroll(Column(std::move(items)).gap(4)).height(140).backgroundColor(t.bg).borderRadius(t.radiusSm),

                    Row({
                            Label(std::to_string(s.items.size()) + " items"),
                            Spacer(),
                            SmallButton(
                                "Clear All",
                                [] {
                                    state().set([](AppState& s) { s.items.clear(); });
                                    setStatus("List cleared", theme().warning);
                                },
                                t.danger),
                        })
                        .alignItems(AlignItems::Center),
                });
}

// ============================================================================
// Counter - Simple interaction demo
// ============================================================================

inline VNode Counter() {
    auto& s = state().use();
    const auto& t = s.theme;

    return Section("Counter",
                   {
                       Row({
                               Button(
                                   "-", [] { state().set([](AppState& s) { s.clickCount--; }); }, t.surfaceAlt),
                               Box(Text_(std::to_string(s.clickCount), t.text, 20.0f))
                                   .minWidth(60)
                                   .justifyContent(JustifyContent::Center)
                                   .alignItems(AlignItems::Center),
                               Button(
                                   "+", [] { state().set([](AppState& s) { s.clickCount++; }); }, t.surfaceAlt),
                           })
                           .gap(t.gap)
                           .alignItems(AlignItems::Center)
                           .justifyContent(JustifyContent::Center),

                       When(s.clickCount > 10, Badge("High score!", t.success)),
                       When(s.clickCount < 0, Badge("Negative!", t.danger)),
                   });
}

// ============================================================================
// Layout Demo - Flexbox features
// ============================================================================

inline VNode LayoutDemo() {
    const auto& t = theme();

    auto swatch = [&](uint32_t color) {
        return Box().width(32).height(32).backgroundColor(color).borderRadius(t.radiusSm);
    };

    return Section("Layout", {
                                 Label("Space between"),
                                 Row({
                                         swatch(t.accent),
                                         swatch(t.success),
                                         swatch(t.warning),
                                     })
                                     .justifyContent(JustifyContent::SpaceBetween)
                                     .backgroundColor(t.bg)
                                     .padding(t.gap)
                                     .borderRadius(t.radiusSm),

                                 Label("Flex grow 1:2:1"),
                                 Row({
                                         Box(Text_("1", t.textOnAccent, 12.0f))
                                             .flexGrow(1)
                                             .height(28)
                                             .backgroundColor(t.accent)
                                             .borderRadius(t.radiusSm)
                                             .justifyContent(JustifyContent::Center)
                                             .alignItems(AlignItems::Center),
                                         Box(Text_("2", t.textOnAccent, 12.0f))
                                             .flexGrow(2)
                                             .height(28)
                                             .backgroundColor(t.success)
                                             .borderRadius(t.radiusSm)
                                             .justifyContent(JustifyContent::Center)
                                             .alignItems(AlignItems::Center),
                                         Box(Text_("1", t.textOnAccent, 12.0f))
                                             .flexGrow(1)
                                             .height(28)
                                             .backgroundColor(t.warning)
                                             .borderRadius(t.radiusSm)
                                             .justifyContent(JustifyContent::Center)
                                             .alignItems(AlignItems::Center),
                                     })
                                     .gap(t.gap),
                             });
}

// ============================================================================
// Scroll Demo
// ============================================================================

inline VNode ScrollDemo() {
    const auto& t = theme();

    std::vector<VNode> rows;
    for (int i = 1; i <= 15; i++) {
        rows.push_back(Box(Label("Row " + std::to_string(i)))
                           .backgroundColor(i % 2 ? t.surfaceAlt : t.surface)
                           .padding(t.gap)
                           .borderRadius(t.radiusSm));
    }

    return Section(
        "Scroll", {
                      Scroll(Column(std::move(rows)).gap(2)).height(120).backgroundColor(t.bg).borderRadius(t.radiusSm),
                  });
}

// ============================================================================
// Keyboard Demo - Shows keyboard event handling
// ============================================================================

// Helper to convert key code to readable name
inline std::string keyCodeToName(int keyCode) {
    if (keyCode >= 32 && keyCode < 127) {
        return std::string(1, static_cast<char>(keyCode));
    }
    // Common special keys (handles both SDL and GLFW key codes)
    switch (keyCode) {
    case 13:
    case 257:
        return "Enter";
    case 27:
    case 256:
        return "Escape";
    case 9:
    case 258:
        return "Tab";
    case 8:
    case 259:
        return "Backspace";
    case 32:
        return "Space";
    case 262:
    case 1073741903:
        return "Right";
    case 263:
    case 1073741904:
        return "Left";
    case 264:
    case 1073741905:
        return "Down";
    case 265:
    case 1073741906:
        return "Up";
    default:
        return "Key(" + std::to_string(keyCode) + ")";
    }
}

// Helper to convert modifier flags to string
inline std::string keyModToString(uint16_t mods) {
    std::string result;
    if (mods & KeyMod_Ctrl)
        result += "Ctrl+";
    if (mods & KeyMod_Shift)
        result += "Shift+";
    if (mods & KeyMod_Alt)
        result += "Alt+";
    if (mods & KeyMod_Super)
        result += "Super+";
    if (result.empty())
        return "None";
    result.pop_back();  // Remove trailing '+'
    return result;
}

// Keyboard event handler - call this from the app root
inline void handleKeyboardEvent(int keyCode, uint16_t mods) {
    state().set([keyCode, mods](AppState& s) {
        s.lastKeyName = keyCodeToName(keyCode);
        s.lastKeyCode = keyCode;
        s.keyModifiers = keyModToString(mods);
    });
}

inline VNode KeyboardDemo() {
    auto& s = state().use();
    const auto& t = s.theme;

    return Section(
        "Keyboard",
        {
            Label("Press any key (click outside inputs first)"),

            Box({
                    Column({
                               Row({Label("Key:"), Body(s.lastKeyName)}).gap(t.gap).alignItems(AlignItems::Center),
                               Row({Label("Code:"), Body(std::to_string(s.lastKeyCode))})
                                   .gap(t.gap)
                                   .alignItems(AlignItems::Center),
                               Row({Label("Mods:"), Body(s.keyModifiers.empty() ? "None" : s.keyModifiers)})
                                   .gap(t.gap)
                                   .alignItems(AlignItems::Center),
                           })
                        .gap(4),
                })
                .backgroundColor(t.bg)
                .borderRadius(t.radiusSm)
                .borderWidth(2)
                .borderColor(t.border)
                .padding(t.padding),
        });
}

}  // namespace showcase
