#pragma once

#include <yui/core/ComponentContext.hpp>
#include <yui/core/Node.hpp>
#include <yui/core/VNode.hpp>
#include <yui/layout/Placement.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace yui::widgets {

// Select (dropdown): a controlled single-select whose OPEN/CLOSED state,
// highlighted row, and popup placement are internal, but whose selected VALUE
// is app-owned (value in, onChange out — the Radio/Slider controlled contract).
//
// Composition (a stateful COMPONENT, like Slider/Tooltip — it needs an element
// ref to anchor the popup and useState to hold open/placement):
//
//   Column( control, [Portal(backdrop(panel))] )
//
//   - control: the always-visible box showing the current selection (or the
//     placeholder), carrying the element ref, the open onClick, and — when a
//     keycode is configured — the nav onKeyDown. It is the placement anchor.
//   - When open, a Portal (root z, hit-tested ABOVE the tree) holds a
//     full-viewport TRANSPARENT backdrop whose onClick closes the popup (the
//     Modal outside-click idiom), with the option panel as its CHILD carrying a
//     consuming onClick([]{}) so interior clicks don't reach the backdrop.
//
// Placement mirrors Tooltip.show() EXACTLY: the open handler reads the
// control's drawn rect (element refs settle in handlers, not in render), walks
// to the render root for the viewport, and calls layout::placePanel to clamp
// the popup on-screen — shifting it UP near the bottom edge (move-don't-shrink)
// and capping its height at maxListHeight (excess scrolls inside a Scroll).
//
// A SECOND click on the control while open lands on the backdrop first (the
// portal hit-order sits above the main tree), so it closes for free — the
// control toggles without any extra wiring.
//
// Keyboard nav is OPT-IN (core is keycode-agnostic — the Modal/Slider
// precedent): the app supplies its toolkit's up/down/select/dismiss codes.
// The nav onKeyDown is attached to the control ONLY WHILE THE POPUP IS OPEN.
// A handler that runs to completion consumes, so an OPEN Select captures every
// key (nav/commit/dismiss drive it; stray keys must not leak to the app content
// behind the open dropdown — it already captures the pointer via the backdrop).
// A CLOSED Select attaches NO onKeyDown, so app hotkeys are never black-holed.
// Consequence: keyboard opens nothing — a Select is opened by click (or by
// app-routed open); the closed control has no key behavior.

// --- Default chrome (override via setters; no theme dependency) ---
inline constexpr uint32_t kSelectControl = 0x2A2A2AFFu;      // control background
inline constexpr uint32_t kSelectHover = 0x333333FFu;        // control hover background
inline constexpr uint32_t kSelectList = 0x1E1E1EFFu;         // popup panel background
inline constexpr uint32_t kSelectOptionHover = 0x333333FFu;  // option hover background
inline constexpr uint32_t kSelectHighlight = 0x4A90D9FFu;    // keyboard-highlighted row
inline constexpr uint32_t kSelectSelected = 0x37373DFFu;     // currently-selected row
inline constexpr uint32_t kSelectBorder = 0x808080FFu;       // control + panel border
inline constexpr uint32_t kSelectText = 0xE0E0E0FFu;         // control + option text
inline constexpr float kSelectFontSize = 14.0f;
inline constexpr float kSelectBorderRadius = 4.0f;
inline constexpr float kSelectBorderWidth = 1.0f;
inline constexpr float kSelectRowHeight = 28.0f;   // estimate for popup height math
inline constexpr float kSelectMaxListH = 240.0f;   // popup height cap; excess scrolls
inline constexpr float kSelectControlPad = 8.0f;
inline constexpr float kSelectOptionPad = 6.0f;

class SelectBuilder {
public:
    explicit SelectBuilder(std::vector<std::string> options) : options_(std::move(options)) {}

    // Selected index; -1 = nothing selected (show the placeholder). App-owned.
    SelectBuilder& value(int v) {
        value_ = v;
        return *this;
    }
    // Fires with the chosen index when the user picks an option. The app
    // re-renders with the new value; the widget never self-selects.
    SelectBuilder& onChange(std::function<void(int)> fn) {
        onChange_ = std::move(fn);
        return *this;
    }
    // Shown in the control when value < 0.
    SelectBuilder& placeholder(std::string text) {
        placeholder_ = std::move(text);
        return *this;
    }
    SelectBuilder& disabled(bool v = true) {
        disabled_ = v;
        return *this;
    }
    // Hard cap on the popup's height; a longer list scrolls inside.
    SelectBuilder& maxListHeight(float h) {
        maxListHeight_ = h;
        return *this;
    }

    // Opt-in keyboard nav: app supplies its toolkit's raw keycodes (the Modal
    // precedent). When ANY is set, an onKeyDown is attached to the control WHILE
    // OPEN and consumes every key; a closed Select attaches nothing (no black-
    // hole) and cannot be opened by keyboard — open by click.
    SelectBuilder& upKeyCode(int keyCode) {
        upKey_ = keyCode;
        return *this;
    }
    SelectBuilder& downKeyCode(int keyCode) {
        downKey_ = keyCode;
        return *this;
    }
    SelectBuilder& selectKeyCode(int keyCode) {
        selectKey_ = keyCode;
        return *this;
    }
    SelectBuilder& dismissKeyCode(int keyCode) {
        dismissKey_ = keyCode;
        return *this;
    }

    // --- Chrome overrides ---
    SelectBuilder& controlColor(uint32_t c) {
        controlColor_ = c;
        return *this;
    }
    SelectBuilder& hoverColor(uint32_t c) {
        hoverColor_ = c;
        return *this;
    }
    SelectBuilder& listColor(uint32_t c) {
        listColor_ = c;
        return *this;
    }
    SelectBuilder& optionHoverColor(uint32_t c) {
        optionHoverColor_ = c;
        return *this;
    }
    SelectBuilder& highlightColor(uint32_t c) {
        highlightColor_ = c;
        return *this;
    }
    SelectBuilder& selectedColor(uint32_t c) {
        selectedColor_ = c;
        return *this;
    }
    SelectBuilder& textColor(uint32_t c) {
        textColor_ = c;
        return *this;
    }
    SelectBuilder& fontSize(float s) {
        fontSize_ = s;
        return *this;
    }
    SelectBuilder& borderRadius(float r) {
        borderRadius_ = r;
        return *this;
    }
    SelectBuilder& borderColor(uint32_t c) {
        borderColor_ = c;
        return *this;
    }
    SelectBuilder& borderWidth(float w) {
        borderWidth_ = w;
        return *this;
    }

    // --- Conversion seam (Child-only, as a Component; the Slider/Tooltip idiom) ---
    operator Child() const& { return Child{build()}; }
    operator Child() && { return Child{build()}; }

private:
    // Popup state: open + placed position/size + highlighted row. ONE slot (the
    // Tooltip Placement precedent — open and its placement always change
    // together, and a single setSt({}) closes and resets everything).
    struct OpenState {
        bool open = false;
        float x = 0, y = 0, w = 0, maxH = 0;
        int highlight = -1;
    };

    Component build() const {
        return Component([options = options_, value = value_, onChange = onChange_, placeholder = placeholder_,
                          disabled = disabled_, maxListHeight = maxListHeight_, upKey = upKey_, downKey = downKey_,
                          selectKey = selectKey_, dismissKey = dismissKey_, controlColor = controlColor_,
                          hoverColor = hoverColor_, listColor = listColor_, optionHoverColor = optionHoverColor_,
                          highlightColor = highlightColor_, selectedColor = selectedColor_, textColor = textColor_,
                          fontSize = fontSize_, borderRadius = borderRadius_, borderColor = borderColor_,
                          borderWidth = borderWidth_](ComponentContext& ctx) -> VNode {
                   auto [st, setSt] = ctx.useState<OpenState>({});
                   NodeRef anchor = ctx.useElementRef();

                   const int n = static_cast<int>(options.size());

                   // Open handler (mirrors Tooltip.show() EXACTLY): read the
                   // control's drawn rect, walk to the render root for the
                   // viewport, and place the popup just below it — clamped
                   // on-screen and height-capped at maxListHeight.
                   auto open = [anchor, setSt, n, value, maxListHeight, disabled] {
                       Node* node = anchor.current();
                       if (!node || disabled)
                           return;
                       layout::Rect a = anchor.getBoundingRect();
                       const Node* root = node;
                       while (root->parent)
                           root = root->parent;
                       auto vp = layout::Viewport::uniform(root->layout.width, root->layout.height);
                       const float estH =
                           std::min(static_cast<float>(n) * kSelectRowHeight, maxListHeight);
                       auto placed = layout::placePanel({a.x, a.bottom()}, {a.w, estH}, vp, maxListHeight);
                       setSt({true, placed.x, placed.y, a.w, placed.height, value});
                   };

                   // --- Control: the always-visible selection box + chevron ---
                   auto label = Text(value >= 0 && value < n ? options[static_cast<size_t>(value)] : placeholder)
                                    .color(textColor)
                                    .fontSize(fontSize);
                   auto chevron = Text(std::string("v")).color(textColor).fontSize(fontSize);

                   auto control = Box(std::move(label), Spacer(), std::move(chevron))
                                      .flexDirection(FlexDirection::Row)
                                      .alignItems(AlignItems::Center)
                                      .padding(kSelectControlPad)
                                      .backgroundColor(controlColor)
                                      .borderRadius(borderRadius)
                                      .borderColor(borderColor)
                                      .borderWidth(borderWidth)
                                      .hoverStyle(BoxStyle{.backgroundColor = hoverColor})
                                      .focusStyle(BoxStyle{.borderColor = highlightColor})
                                      .cursor(disabled ? CursorShape::Arrow : CursorShape::Pointer)
                                      .focusable(!disabled)
                                      .ref(anchor)
                                      .setKey("control")
                                      .onClick(open);

                   // NOTE: we do NOT close on blur. Clicking an option row (which
                   // is not focusable) clears focus from the control — a blur-to-
                   // close handler would fire on that click's focus change and
                   // dismiss the list BEFORE the option's onClick commits, so no
                   // selection would ever land. Click-away is already handled by
                   // the transparent full-viewport backdrop (its onClick closes),
                   // and Esc dismisses via the keyboard path; blur-close is both
                   // redundant with those and races the option click, so it's gone.

                   // Opt-in keyboard nav. Attached to the control ONLY WHILE
                   // OPEN and only when a keycode is set; once attached it
                   // consumes every key while focused (nav/commit/dismiss drive
                   // the open popup; stray keys must not leak to app content
                   // behind it). A closed Select attaches nothing — no black-
                   // hole, and no keyboard-open (open by click).
                   if (st.open && (upKey || downKey || selectKey || dismissKey)) {
                       control.onKeyDown([setSt, st = st, n, onChange, upKey, downKey, selectKey,
                                          dismissKey](int keyCode, uint16_t /*mods*/, bool /*repeat*/) {
                           if (dismissKey && keyCode == *dismissKey) {
                               setSt({});
                           } else if (upKey && keyCode == *upKey) {
                               OpenState ns = st;
                               ns.highlight = std::max(0, st.highlight - 1);
                               setSt(ns);
                           } else if (downKey && keyCode == *downKey) {
                               OpenState ns = st;
                               ns.highlight = std::min(n - 1, st.highlight + 1);
                               setSt(ns);
                           } else if (selectKey && keyCode == *selectKey) {
                               if (st.highlight >= 0 && st.highlight < n && onChange)
                                   onChange(st.highlight);
                               setSt({});
                           }
                       });
                   }

                   std::vector<Child> kids;
                   kids.emplace_back(std::move(control));

                   if (st.open) {
                       // Option rows: highlighted row wins over selected row wins
                       // over transparent (omit the backgroundColor call).
                       std::vector<Child> rows;
                       rows.reserve(options.size());
                       for (int i = 0; i < n; ++i) {
                           auto row = Box(Text(options[static_cast<size_t>(i)]).color(textColor).fontSize(fontSize))
                                          .padding(kSelectOptionPad)
                                          .setKey(std::to_string(i))
                                          .hoverStyle(BoxStyle{.backgroundColor = optionHoverColor})
                                          .cursor(CursorShape::Pointer)
                                          .onClick([onChange, setSt, i, value] {
                                              // Re-selecting the current value fires no onChange
                                              // (the Radio/Slider dedup contract), but any option
                                              // click still dismisses the popup.
                                              if (i != value && onChange)
                                                  onChange(i);
                                              setSt({});
                                          });
                           if (i == st.highlight)
                               row.backgroundColor(highlightColor);
                           else if (i == value)
                               row.backgroundColor(selectedColor);
                           rows.emplace_back(std::move(row));
                       }

                       // Panel: the option list, scrolling inside the placed
                       // height cap. Consuming onClick([]{}) is load-bearing (the
                       // Modal precedent) — an interior click must NOT bubble to
                       // the backdrop and close the popup.
                       VNode panel = Box(Scroll(Column(std::move(rows))).heightPercent(100))
                                         .width(st.w)
                                         .height(st.maxH)
                                         .backgroundColor(listColor)
                                         .borderRadius(borderRadius)
                                         .borderColor(borderColor)
                                         .borderWidth(borderWidth)
                                         .positionType(PositionType::Absolute)
                                         .positionLeft(st.x)
                                         .positionTop(st.y)
                                         .setKey("list")
                                         .onClick([] {});

                       // Backdrop: full-viewport, TRANSPARENT (no backgroundColor).
                       // Outside click closes; the consuming onScroll stops the
                       // wheel from bubbling through the portal into a logical
                       // ancestor Scroll (the Modal precedent).
                       VNode backdrop = Box(std::move(panel))
                                            .positionType(PositionType::Absolute)
                                            .positionLeft(0)
                                            .positionTop(0)
                                            .widthPercent(100)
                                            .heightPercent(100)
                                            .onClick([setSt] { setSt({}); })
                                            .onScroll([](float, float) {});

                       kids.emplace_back(Portal(std::move(backdrop)));
                   }

                   return Column(std::move(kids));
               })
            .setName("Select");
    }

    std::vector<std::string> options_;
    int value_ = -1;
    std::function<void(int)> onChange_;
    std::string placeholder_ = "Select...";
    bool disabled_ = false;
    float maxListHeight_ = kSelectMaxListH;
    std::optional<int> upKey_;
    std::optional<int> downKey_;
    std::optional<int> selectKey_;
    std::optional<int> dismissKey_;
    uint32_t controlColor_ = kSelectControl;
    uint32_t hoverColor_ = kSelectHover;
    uint32_t listColor_ = kSelectList;
    uint32_t optionHoverColor_ = kSelectOptionHover;
    uint32_t highlightColor_ = kSelectHighlight;
    uint32_t selectedColor_ = kSelectSelected;
    uint32_t textColor_ = kSelectText;
    float fontSize_ = kSelectFontSize;
    float borderRadius_ = kSelectBorderRadius;
    uint32_t borderColor_ = kSelectBorder;
    float borderWidth_ = kSelectBorderWidth;
};

// --- Factory ---

// Select over `options` (controlled: pass .value + .onChange).
[[nodiscard]] inline SelectBuilder Select(std::vector<std::string> options) {
    return SelectBuilder{std::move(options)};
}

}  // namespace yui::widgets
