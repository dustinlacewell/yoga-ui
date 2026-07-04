#pragma once

#include <yui/core/VNode.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace yui::widgets {

// Modal: a dialog over a scrim, composed entirely from the Portal primitive —
// `Portal(backdrop(panel)).trapFocus()`. No core involvement:
//
//   - The portal renders backdrop + panel at root z-order, escaping any
//     ancestor clip, and hit-tests ABOVE the main tree — so the full-viewport
//     backdrop catches every click/scroll aimed at content behind the modal.
//   - .trapFocus() scopes Tab to the modal's content, saves the pre-modal
//     focus at mount, and restores it at unmount (the F2 contract) — a Modal
//     gets trap + save + restore with no wiring of its own.
//   - Open/close is app state: render the Modal while open, stop rendering it
//     to close. Mounting arms the trap, unmounting restores focus. The widget
//     holds no Host reference and needs none.
//
// The PANEL (the user's children, wrapped) is a CHILD of the backdrop, which
// centers it. That nesting is what makes the dismissal semantics compose from
// plain bubbling: a click inside the dialog bubbles toward the backdrop and
// must be consumed at the panel, a click beside the dialog hits the backdrop
// directly and dismisses.
class ModalBuilder {
public:
    explicit ModalBuilder(std::vector<Child> children) : children_(std::move(children)) {}

    // Called when the user asks to dismiss: a backdrop click (if enabled
    // below) or the dismiss key. The app closes the modal by dropping it from
    // the next render — the widget never closes itself.
    ModalBuilder& onDismiss(std::function<void()> fn) {
        onDismiss_ = std::move(fn);
        return *this;
    }

    // Clicking the backdrop (outside the panel) calls onDismiss. Default true.
    // When false the backdrop still consumes the click — the modal stays
    // modal, the click just does nothing.
    ModalBuilder& dismissOnBackdropClick(bool v = true) {
        dismissOnBackdropClick_ = v;
        return *this;
    }

    // Raw platform keycode that dismisses the modal (e.g. the app passes its
    // toolkit's Esc code). App-supplied, the Tab precedent: core stays
    // keycode-agnostic because SDL and GLFW disagree on codes. Unset = no key
    // dismiss.
    ModalBuilder& dismissKeyCode(int keyCode) {
        dismissKeyCode_ = keyCode;
        return *this;
    }

    // Scrim fill (RGBA). Default: half-transparent black.
    ModalBuilder& backdropColor(uint32_t color) {
        backdropColor_ = color;
        return *this;
    }

    // --- Conversion seam (mirrors BuilderBase): the tree is built at the
    // point the Modal is placed into a parent's children. ---
    operator VNode() const& { return build(children_); }
    operator VNode() && { return build(std::move(children_)); }
    operator Child() const& { return Child{build(children_)}; }
    operator Child() && { return Child{build(std::move(children_))}; }

private:
    VNode build(std::vector<Child> children) const {
        auto dismiss = onDismiss_;

        // Every key that bubbles to the panel stops here (a handler that runs
        // to completion consumes): the dismiss key dismisses, anything else
        // must not fall through the modal into app hotkeys behind it. Keys
        // reach the panel by bubbling from the focused node, which the trap
        // keeps inside the modal.
        auto panelKeyDown = [dismiss, key = dismissKeyCode_](int keyCode, uint16_t /*mods*/, bool /*repeat*/) {
            if (key && keyCode == *key && dismiss)
                dismiss();
        };

        // The panel's consuming onClick is load-bearing, not hygiene: the
        // backdrop is the panel's PARENT, so an unconsumed click inside the
        // dialog would bubble to the backdrop and dismiss.
        VNode panel = Box(std::move(children)).onClick([] {}).onKeyDown(std::move(panelKeyDown));

        // Backdrop click consumes either way: dismissal is the affordance;
        // with it disabled this is belt-and-suspenders on top of the portal
        // hit-order, which already keeps the click off the tree behind.
        auto backdropClick = [dismiss, enabled = dismissOnBackdropClick_] {
            if (enabled && dismiss)
                dismiss();
        };

        // Full-viewport scrim (portal content lays out against the viewport),
        // centering the panel. The consuming onScroll blocks the wheel: the
        // hit-order keeps scroll off content BEHIND the modal, but an
        // unconsumed scroll would still bubble through the portal into the
        // logical parent chain — e.g. a Scroll the modal was declared inside.
        VNode backdrop = Box(std::move(panel))
                             .positionType(PositionType::Absolute)
                             .positionLeft(0)
                             .positionTop(0)
                             .widthPercent(100)
                             .heightPercent(100)
                             .backgroundColor(backdropColor_)
                             .justifyContent(JustifyContent::Center)
                             .alignItems(AlignItems::Center)
                             .onClick(std::move(backdropClick))
                             .onScroll([](float, float) {});

        return Portal(std::move(backdrop)).trapFocus();
    }

    std::vector<Child> children_;
    std::function<void()> onDismiss_;
    bool dismissOnBackdropClick_ = true;
    std::optional<int> dismissKeyCode_;
    uint32_t backdropColor_ = 0x00000088u;
};

// --- Factory functions (the Box/Portal factory idiom) ---

// Modal with children (dynamic path: a runtime-built vector).
[[nodiscard]] inline ModalBuilder Modal(std::vector<Child> children) {
    return ModalBuilder{std::move(children)};
}

// Static path: each argument forwarded into a Child slot (see Box variadic).
template <class... Cs, typename = std::enable_if_t<!is_single_child_vector_v<Cs...>>>
[[nodiscard]] inline ModalBuilder Modal(Cs&&... cs) {
    std::vector<Child> v;
    v.reserve(sizeof...(Cs));
    (v.emplace_back(std::forward<Cs>(cs)), ...);
    return ModalBuilder{std::move(v)};
}

}  // namespace yui::widgets
