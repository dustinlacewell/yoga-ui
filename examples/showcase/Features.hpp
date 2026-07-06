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

inline Component ThemeSwitcher() {
    return [](ComponentContext&) -> VNode {
        auto s = state().use();
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

        return Column(
                          Row(
                                  themeButton("Midnight", 0),
                                  themeButton("Ember", 1),
                                  themeButton("Violet", 2),
                                  themeButton("Mono", 3),
                                  themeButton("Cyber", 4),
                                  themeButton("Forest", 5)
                              )
                              .gap(t.gap)
                              .flexWrap(FlexWrap::Wrap),
                          Row(
                                  Label("Size:"),
                                  sizeButton("Default", 0),
                                  sizeButton("Compact", 1),
                                  sizeButton("Spacious", 2)
                              )
                              .gap(t.gap)
                              .alignItems(AlignItems::Center)
                      )
            .gap(t.gap);
    };
}

// ============================================================================
// Login Form - Forms, validation, inputs (using useField hook)
// ============================================================================

inline auto LoginForm() -> Component {
    return [](ComponentContext& ctx) -> VNode {
        // useField binds Store fields to inputs with minimal boilerplate
        auto [username, setUsername] = ctx.useField(state(), &AppState::username);
        auto [password, setPassword] = ctx.useField(state(), &AppState::password);
        const auto& t = theme();

        return Section(
            "Login",
            {
                Column(Label("Username"), TextInput(username, setUsername, "Enter username").autoFocus()).gap(4).flexGrow(1),
                LabeledInput("Password", password, setPassword, "Password", true),

                Row(
                        Button(
                            "Sign In",
                            [] {
                                auto s = state().use();
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
                            t.surfaceAlt)
                    )
                    .gap(t.gap),
            });
    };
}

// ============================================================================
// Dynamic List - Add/remove, keyed lists
// ============================================================================

inline VNode ListItem(const std::string& text, size_t index) {
    const auto& t = theme();

    return Box(
                   Row(
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
                                      })
                       )
                       .alignItems(AlignItems::Center)
               )
        .backgroundColor(t.surfaceAlt)
        .borderRadius(t.radiusSm)
        .padding(t.gap)
        .hoverStyle(BoxStyle{.backgroundColor = t.border})
        .setKey("item-" + std::to_string(index));
}

inline auto DynamicList() -> Component {
    return [](ComponentContext& ctx) -> VNode {
        auto s = state().use();
        const auto& t = s.theme;

        // useField for the input text
        auto [newItemText, setNewItemText] = ctx.useField(state(), &AppState::newItemText);

        std::vector<Child> items;
        for (size_t i = 0; i < s.items.size(); i++) {
            items.push_back(ListItem(s.items[i], i));
        }

        return Section(
            "List", {
                        Row(
                                TextInput(newItemText, setNewItemText, "New item...").flexGrow(1),
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
                                    t.success)
                            )
                            .gap(t.gap)
                            .alignItems(AlignItems::Center),

                        Scroll(Column(std::move(items)).gap(4)).height(140).backgroundColor(t.bg).borderRadius(t.radiusSm),

                        Row(
                                Label(std::to_string(s.items.size()) + " items"),
                                Spacer(),
                                SmallButton(
                                    "Clear All",
                                    [] {
                                        state().set([](AppState& s) { s.items.clear(); });
                                        setStatus("List cleared", theme().warning);
                                    },
                                    t.danger)
                            )
                            .alignItems(AlignItems::Center),
                    });
    };
}

// ============================================================================
// Counter - Simple interaction demo
// ============================================================================

inline Component Counter() {
    return [](ComponentContext&) -> VNode {
        auto s = state().use();
        const auto& t = s.theme;

        return Section("Counter",
                       {
                           Row(
                                   Button(
                                       "-", [] { state().set([](AppState& s) { s.clickCount--; }); }, t.surfaceAlt),
                                   Box(Text_(std::to_string(s.clickCount), t.text, 20.0f))
                                       .minWidth(60)
                                       .justifyContent(JustifyContent::Center)
                                       .alignItems(AlignItems::Center),
                                   Button(
                                       "+", [] { state().set([](AppState& s) { s.clickCount++; }); }, t.surfaceAlt)
                               )
                               .gap(t.gap)
                               .alignItems(AlignItems::Center)
                               .justifyContent(JustifyContent::Center),

                           When(s.clickCount > 10, Badge("High score!", t.success)),
                           When(s.clickCount < 0, Badge("Negative!", t.danger)),
                       });
    };
}

// ============================================================================
// Layout Demo - Flexbox features
// ============================================================================

inline Component LayoutDemo() {
    return [](ComponentContext&) -> VNode {
        const auto& t = theme();

        auto swatch = [&](uint32_t color) {
            return Box().width(32).height(32).backgroundColor(color).borderRadius(t.radiusSm);
        };

        return Section("Layout", {
                                     Label("Space between"),
                                     Row(
                                             swatch(t.accent),
                                             swatch(t.success),
                                             swatch(t.warning)
                                         )
                                         .justifyContent(JustifyContent::SpaceBetween)
                                         .backgroundColor(t.bg)
                                         .padding(t.gap)
                                         .borderRadius(t.radiusSm),

                                     Label("Flex grow 1:2:1"),
                                     Row(
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
                                                 .alignItems(AlignItems::Center)
                                         )
                                         .gap(t.gap),
                                 });
    };
}

// ============================================================================
// Scroll Demo
// ============================================================================

inline Component ScrollDemo() {
    return [](ComponentContext&) -> VNode {
        const auto& t = theme();

        std::vector<Child> rows;
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
    };
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

inline Component KeyboardDemo() {
    return [](ComponentContext&) -> VNode {
        auto s = state().use();
        const auto& t = s.theme;

        return Section(
            "Keyboard",
            {
                Label("Press any key (click outside inputs first)"),

                Box(
                        Column(
                                   Row(Label("Key:"), Body(s.lastKeyName)).gap(t.gap).alignItems(AlignItems::Center),
                                   Row(Label("Code:"), Body(std::to_string(s.lastKeyCode)))
                                       .gap(t.gap)
                                       .alignItems(AlignItems::Center),
                                   Row(Label("Mods:"), Body(s.keyModifiers.empty() ? "None" : s.keyModifiers))
                                       .gap(t.gap)
                                       .alignItems(AlignItems::Center)
                               )
                            .gap(4)
                    )
                    .backgroundColor(t.bg)
                    .borderRadius(t.radiusSm)
                    .borderWidth(2)
                    .borderColor(t.border)
                    .padding(t.padding),
            });
    };
}

// ============================================================================
// Hooks Demo - Demonstrates useState hook with local component state
// ============================================================================

// A stateful counter component using hooks - state is local to each instance
inline Component HookCounter() {
    return [](ComponentContext& ctx) -> VNode {
        auto [count, setCount] = ctx.useState<int>(0);
        const auto& t = theme();

        return Row(
                       Box(Text_("-", t.textOnAccent, 16.0f))
                           .backgroundColor(t.surfaceAlt)
                           .borderRadius(t.radiusSm)
                           .padding(8)
                           .onClick([setCount, count] { setCount(count - 1); })
                           .hoverStyle(BoxStyle{.backgroundColor = t.border}),
                       Box(Text_(std::to_string(count), t.text, 18.0f))
                           .minWidth(50)
                           .justifyContent(JustifyContent::Center)
                           .alignItems(AlignItems::Center),
                       Box(Text_("+", t.textOnAccent, 16.0f))
                           .backgroundColor(t.surfaceAlt)
                           .borderRadius(t.radiusSm)
                           .padding(8)
                           .onClick([setCount, count] { setCount(count + 1); })
                           .hoverStyle(BoxStyle{.backgroundColor = t.border})
                   )
            .gap(8)
            .alignItems(AlignItems::Center);
    };
}

// A toggle component that demonstrates boolean state
inline Component HookToggle(const std::string& label) {
    return [label](ComponentContext& ctx) -> VNode {
        auto [on, setOn] = ctx.useState<bool>(false);
        const auto& t = theme();

        return Row(
                       Text_(label, t.text, 12.0f),
                       Box(Box()
                               .width(16)
                               .height(16)
                               .backgroundColor(on ? t.success : t.bg)
                               .borderRadius(8)
                               .positionType(PositionType::Absolute)
                               .positionLeft(on ? 18.0f : 2.0f)
                               .positionTop(2.0f))
                           .width(36)
                           .height(20)
                           .backgroundColor(on ? t.success : t.border)
                           .borderRadius(10)
                           .onClick([setOn, on] { setOn(!on); })
                   )
            .gap(8)
            .alignItems(AlignItems::Center);
    };
}

inline Component HooksDemo() {
    return [](ComponentContext&) -> VNode {
        const auto& t = theme();

        return Section("Hooks",
                       {
                           Label("Local component state with useState:"),
                           Column(
                                      Row(Label("Counter 1:"), HookCounter()).gap(t.gap).alignItems(AlignItems::Center),
                                      Row(Label("Counter 2:"), HookCounter()).gap(t.gap).alignItems(AlignItems::Center),
                                      HookToggle("Feature A"),
                                      HookToggle("Feature B")
                                  )
                               .gap(t.gap),
                       });
    };
}

// ============================================================================
// Widgets Demo - The yui::widgets layer (Button, Checkbox, Radio, Progress,
// Tabs, Slider, Select). Every control is CONTROLLED: the value lives in the
// Store and each widget's onChange writes it back, so the demo round-trips
// through the same reactive state as every other feature.
// ============================================================================

// App-supplied keycodes for widget keyboard nav. Core is keycode-agnostic, so
// each widget takes raw codes; these cover both SDL and GLFW (the showcase
// keyCodeToName table already carries both). Arrows drive Slider/Select/Tabs,
// Enter commits a Select option, Esc dismisses it, Space toggles a Checkbox.
namespace wkeys {
inline constexpr int kSpace = 32;
inline constexpr int kEnterSDL = 13, kEnterGLFW = 257;
inline constexpr int kEscSDL = 27, kEscGLFW = 256;
inline constexpr int kLeftSDL = 1073741904, kLeftGLFW = 263;
inline constexpr int kRightSDL = 1073741903, kRightGLFW = 262;
inline constexpr int kUpSDL = 1073741906, kUpGLFW = 265;
inline constexpr int kDownSDL = 1073741905, kDownGLFW = 264;
}  // namespace wkeys

inline Component WidgetsDemo() {
    return [](ComponentContext&) -> VNode {
        auto s = state().use();
        const auto& t = s.theme;

        using namespace yui::widgets;  // Button/Checkbox/etc. — note showcase has
                                       // its own primitive Button, so qualify.

        // --- Buttons: enabled fires, disabled is inert but still swallows the
        // click (native-button parity). ---
        auto buttons = Row(
                               widgets::Button("Press me")
                                   .backgroundColor(t.accent)
                                   .hoverColor(t.accentHover)
                                   .textColor(t.textOnAccent)
                                   .borderRadius(t.radiusSm)
                                   .onClick([] { state().set([](AppState& s) { s.buttonPresses++; }); }),
                               widgets::Button("Disabled").disabled().borderRadius(t.radiusSm),
                               Body(std::to_string(s.buttonPresses) + " presses")
                           )
                           .gap(t.gap)
                           .alignItems(AlignItems::Center);

        // --- Switches: sliding-pill on/off toggles (controlled bool + Space to
        // toggle when focused). ---
        auto switches = Column(
                            Switch(s.notifications)
                                .label("Enable notifications")
                                .trackOnColor(t.accent)
                                .toggleKeyCode(wkeys::kSpace)
                                .onChange([](bool v) { state().set([v](AppState& s) { s.notifications = v; }); }),
                            Switch(s.darkMode)
                                .label("Dark mode")
                                .trackOnColor(t.accent)
                                .toggleKeyCode(wkeys::kSpace)
                                .onChange([](bool v) { state().set([v](AppState& s) { s.darkMode = v; }); }))
                            .gap(t.gap);

        // --- Checkbox: box + inset check mark (controlled bool). ---
        auto check = Checkbox(s.acceptTerms)
                         .label("I accept the terms")
                         .checkColor(t.accent)
                         .toggleKeyCode(wkeys::kSpace)
                         .onChange([](bool v) { state().set([v](AppState& s) { s.acceptTerms = v; }); });

        // --- Radio group (controlled index). ---
        auto radios = RadioGroup({"Free", "Pro", "Team"})
                          .value(s.planIndex)
                          .dotColor(t.accent)
                          .onChange([](int i) { state().set([i](AppState& s) { s.planIndex = i; }); });

        // --- Slider (controlled float, drives the Progress bar below it).
        // Wrapped so the track has a definite width (Slider is width:100%). ---
        auto slider = Column(
                          Row(Label("Volume"), Spacer(), Body(std::to_string(static_cast<int>(s.volume * 100)) + "%"))
                              .alignItems(AlignItems::Center),
                          Box(Slider(s.volume)
                                  .fillColor(t.accent)
                                  .step(0.01f)
                                  .decrementKeyCode(wkeys::kLeftSDL)
                                  .incrementKeyCode(wkeys::kRightSDL)
                                  .onChange([](float v) { state().set([v](AppState& s) { s.volume = v; }); }))
                              .width(220),
                          // Progress mirrors the slider value — the one purely
                          // visual widget.
                          Box(Progress(s.volume).fillColor(t.success)).width(220))
                          .gap(t.gap);

        // --- Tabs (controlled active index; only the active panel renders). ---
        auto tabs = Tabs()
                        .active(s.tabIndex)
                        .activeTabColor(t.accent)
                        .prevKeyCode(wkeys::kLeftGLFW)
                        .nextKeyCode(wkeys::kRightGLFW)
                        .tab("Overview", Box(Body("The overview panel.")).padding(t.padding))
                        .tab("Details", Box(Body("Details live here.")).padding(t.padding))
                        .tab("Settings", Box(Body("And settings on the third tab.")).padding(t.padding))
                        .onChange([](int i) { state().set([i](AppState& s) { s.tabIndex = i; }); });

        // --- Select (controlled index; opens a Portal option list). Arrows move
        // the highlight, Enter commits, Esc dismisses. ---
        auto select = Row(
                          Label("Fruit:"),
                          Box(Select({"Apple", "Banana", "Cherry", "Date", "Elderberry", "Fig", "Grape"})
                                  .value(s.fruitIndex)
                                  .placeholder("Pick one...")
                                  .highlightColor(t.accent)
                                  .upKeyCode(wkeys::kUpGLFW)
                                  .downKeyCode(wkeys::kDownGLFW)
                                  .selectKeyCode(wkeys::kEnterGLFW)
                                  .dismissKeyCode(wkeys::kEscGLFW)
                                  .onChange([](int i) { state().set([i](AppState& s) { s.fruitIndex = i; }); }))
                              .width(180))
                          .gap(t.gap)
                          .alignItems(AlignItems::Center);

        return Section("Widgets", {
                                      Label("Buttons"),
                                      buttons,
                                      Label("Switches"),
                                      switches,
                                      Label("Checkbox"),
                                      check,
                                      Label("Radio group"),
                                      radios,
                                      Label("Slider + Progress"),
                                      slider,
                                      Label("Tabs"),
                                      tabs,
                                      Label("Select"),
                                      select,
                                  });
    };
}

// ============================================================================
// Right-Click Demo - Demonstrates onRightClick event handling
// ============================================================================

inline Component RightClickDemo() {
    return [](ComponentContext& ctx) -> VNode {
        auto [message, setMessage] = ctx.useState<std::string>("Right-click or left-click the boxes");
        const auto& t = theme();

        auto clickBox = [&](const std::string& label, uint32_t bg) {
            return Box(Text_(label, t.textOnAccent, 12.0f))
                .backgroundColor(bg)
                .borderRadius(t.radiusSm)
                .padding(t.padding)
                .flexGrow(1)
                .justifyContent(JustifyContent::Center)
                .alignItems(AlignItems::Center)
                .hoverStyle(BoxStyle{.backgroundColor = t.border})
                .onClick([setMessage, label] { setMessage("Left-clicked: " + label); })
                .onRightClick([setMessage, label] { setMessage("Right-clicked: " + label); });
        };

        return Section("Right-Click",
                       {
                           Body(message),
                           Row(
                                   clickBox("Box A", t.accent),
                                   clickBox("Box B", t.success),
                                   clickBox("Box C", t.warning)
                               )
                               .gap(t.gap),
                       });
    };
}

// ============================================================================
// Textarea Demo - Demonstrates a multiline Input (Enter inserts a newline,
// the box grows with its wrapped lines, Up/Down navigate by visual line)
// ============================================================================

inline Component TextAreaDemo() {
    return [](ComponentContext& ctx) -> VNode {
        auto [text, setText] = ctx.useState<std::string>("Multiline input.\nEnter adds a line;\nUp/Down move by line.");
        const auto& t = theme();

        return Section("Textarea",
                       {
                           Input()
                               .value(text)
                               .onChange([setText](const std::string& v) { setText(v); })
                               .multiline()
                               .placeholder("Type multiple lines...")
                               .color(t.text)
                               .backgroundColor(t.bg)
                               .borderColor(t.border)
                               .borderWidth(1)
                               .borderRadius(t.radiusSm)
                               .hoverStyle(InputStyle{.borderColor = t.textMuted})
                               .focusStyle(InputStyle{.borderColor = t.accent}),
                           Label("Grows to fit its wrapped lines"),
                       });
    };
}

// ============================================================================
// AutoFocus Demo - Demonstrates autoFocus on Input
// ============================================================================

inline Component AutoFocusDemo() {
    return [](ComponentContext& ctx) -> VNode {
        auto [visible, setVisible] = ctx.useState<bool>(false);
        auto [text, setText] = ctx.useState<std::string>("");
        const auto& t = theme();

        std::vector<Child> children;
        children.push_back(Label("Toggle to show an auto-focused input:"));
        children.push_back(
            Row(
                    Button(visible ? "Hide" : "Show Input", [setVisible, visible] { setVisible(!visible); }, t.surfaceAlt),
                    Spacer(),
                    When(!text.empty(), Badge("Typed: " + text, t.surfaceAlt))
                )
                .alignItems(AlignItems::Center));

        if (visible) {
            children.push_back(
                Input()
                    .value(text)
                    .onChange([setText](const std::string& v) { setText(v); })
                    .placeholder("I'm auto-focused!")
                    .autoFocus()
                    .height(32)
                    .color(t.text)
                    .backgroundColor(t.bg)
                    .borderColor(t.accent)
                    .borderWidth(2)
                    .borderRadius(t.radiusSm)
                    .focusStyle(InputStyle{.borderColor = t.success}));
        }

        return Section("AutoFocus", std::move(children));
    };
}

}  // namespace showcase
