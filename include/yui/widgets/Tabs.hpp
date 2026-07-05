#pragma once

#include <yui/core/VNode.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace yui::widgets {

// Tabs: a horizontal strip of tab buttons over a single content panel. A PURE
// builder (no transient state): the active index is CONTROLLED — the app owns
// it, passes it via .active(i), and updates it in .onChange(i). The widget
// holds no useState; it just renders the strip + the active panel.
//
// Composition:
//
//   Column(
//     Row( for each tab: Box(Text(label)).onClick(-> onChange(i)) ).bg(strip),
//     Box(panels[active]).setKey("panel-<active>")     <- the content panel
//   )
//
// REMOUNT-ON-SWITCH (the load-bearing contract): only the ACTIVE panel is
// instantiated, and its wrapper is keyed "panel-<active>". Switching tabs
// changes the key, so the reconciler UNMOUNTS the old panel and MOUNTS the new
// one rather than reconciling one panel's tree into another. Consequence: a
// panel's component state (useState/useRef) is LOST when you switch away and
// does NOT persist on return. This mirrors Modal's "render while open" model —
// an inactive tab is simply not rendered. Apps that need a tab's state to
// survive switching must LIFT that state into a Store above the Tabs.
//
// Keyboard nav is OPT-IN: prev/next keycodes are app-supplied (core is
// keycode-agnostic). When configured, the strip becomes focusable and its
// onKeyDown cycles active±1 (wrapping) via onChange. That handler CONSUMES
// every key while the strip is focused (a handler that runs to completion
// consumes) — configure the nav codes only when the strip should own the
// keyboard while focused.

inline constexpr uint32_t kTabStrip = 0x1E1E1EFFu;
inline constexpr uint32_t kTab = 0x2A2A2AFFu;
inline constexpr uint32_t kTabActive = 0x3A3A3AFFu;
inline constexpr uint32_t kTabHover = 0x333333FFu;
inline constexpr uint32_t kTabText = 0x808080FFu;
inline constexpr uint32_t kTabActiveText = 0xFFFFFFFFu;
inline constexpr float kTabFontSize = 14.0f;
inline constexpr float kTabPad = 8.0f;

class TabsBuilder {
public:
    TabsBuilder() = default;

    // Accumulator: append a (label, panel) pair. Call once per tab, in order.
    TabsBuilder& tab(std::string label, Child panel) {
        labels_.push_back(std::move(label));
        panels_.push_back(std::move(panel));
        return *this;
    }

    TabsBuilder& active(int index) {
        active_ = index;
        return *this;
    }

    TabsBuilder& onChange(std::function<void(int)> fn) {
        onChange_ = std::move(fn);
        return *this;
    }

    // Opt-in arrow navigation: app-supplied raw keycodes (the Modal precedent).
    // When either is set the strip is focusable and cycles active on the key.
    TabsBuilder& prevKeyCode(int keyCode) {
        prevKeyCode_ = keyCode;
        return *this;
    }
    TabsBuilder& nextKeyCode(int keyCode) {
        nextKeyCode_ = keyCode;
        return *this;
    }

    TabsBuilder& stripColor(uint32_t v) {
        stripColor_ = v;
        return *this;
    }
    TabsBuilder& tabColor(uint32_t v) {
        tabColor_ = v;
        return *this;
    }
    TabsBuilder& activeTabColor(uint32_t v) {
        activeTabColor_ = v;
        return *this;
    }
    TabsBuilder& hoverTabColor(uint32_t v) {
        hoverTabColor_ = v;
        return *this;
    }
    TabsBuilder& textColor(uint32_t v) {
        textColor_ = v;
        return *this;
    }
    TabsBuilder& activeTextColor(uint32_t v) {
        activeTextColor_ = v;
        return *this;
    }
    TabsBuilder& fontSize(float v) {
        fontSize_ = v;
        return *this;
    }
    TabsBuilder& tabPadding(float v) {
        tabPadding_ = v;
        return *this;
    }

    // --- Conversion seam (mirrors Modal: build at the point of placement) ---
    operator VNode() const& { return build(); }
    operator VNode() && { return build(); }
    operator Child() const& { return Child{build()}; }
    operator Child() && { return Child{build()}; }

private:
    VNode build() const {
        const int n = static_cast<int>(labels_.size());
        const int active = (n > 0) ? std::clamp(active_, 0, n - 1) : 0;

        std::vector<Child> tabs;
        tabs.reserve(labels_.size());
        for (int i = 0; i < n; ++i) {
            const bool isActive = (i == active);
            auto onChange = onChange_;
            auto tabBox = Box(Text(labels_[i]).color(isActive ? activeTextColor_ : textColor_).fontSize(fontSize_))
                              .setKey(i)
                              .paddingTop(tabPadding_)
                              .paddingBottom(tabPadding_)
                              .paddingLeft(tabPadding_ * 1.5f)
                              .paddingRight(tabPadding_ * 1.5f)
                              .backgroundColor(isActive ? activeTabColor_ : tabColor_)
                              .cursor(CursorShape::Pointer)
                              .onClick([onChange, i, active] {
                                  if (i != active && onChange)
                                      onChange(i);
                              });
            // Hover fill on inactive tabs only: the active tab is already lit.
            if (!isActive)
                tabBox.hoverStyle(BoxStyle{.backgroundColor = hoverTabColor_});
            tabs.emplace_back(std::move(tabBox));
        }

        auto strip = Box(std::move(tabs)).flexDirection(FlexDirection::Row).backgroundColor(stripColor_);

        // Opt-in keyboard: focusable strip cycling active±1 (wrapping). Only
        // wired when a nav keycode is configured — otherwise NO onKeyDown, so
        // the strip consumes nothing.
        if ((prevKeyCode_ || nextKeyCode_) && n > 0) {
            auto onChange = onChange_;
            strip.focusable(true).onKeyDown([onChange, prev = prevKeyCode_, next = nextKeyCode_, active,
                                             n](int keyCode, uint16_t, bool) {
                if (!onChange)
                    return;
                if (next && keyCode == *next)
                    onChange((active + 1) % n);
                else if (prev && keyCode == *prev)
                    onChange((active - 1 + n) % n);
            });
        }

        std::vector<Child> panelWrap;
        if (active < static_cast<int>(panels_.size()))
            panelWrap.push_back(panels_[active]);  // copy: repeatable render

        // Key the panel wrapper per active index so switching tabs REMOUNTS the
        // panel (see class note) rather than reconciling state across tabs.
        auto panel =
            Box(std::move(panelWrap)).flexGrow(1).setKey(std::string("panel-") + std::to_string(active));

        return Column(std::move(strip), std::move(panel));
    }

    std::vector<std::string> labels_;
    std::vector<Child> panels_;
    int active_ = 0;
    std::function<void(int)> onChange_;
    std::optional<int> prevKeyCode_;
    std::optional<int> nextKeyCode_;
    uint32_t stripColor_ = kTabStrip;
    uint32_t tabColor_ = kTab;
    uint32_t activeTabColor_ = kTabActive;
    uint32_t hoverTabColor_ = kTabHover;
    uint32_t textColor_ = kTabText;
    uint32_t activeTextColor_ = kTabActiveText;
    float fontSize_ = kTabFontSize;
    float tabPadding_ = kTabPad;
};

// --- Factory function ---

// Tabs: start empty, add tabs via .tab(label, panel).
[[nodiscard]] inline TabsBuilder Tabs() {
    return TabsBuilder{};
}

}  // namespace yui::widgets
