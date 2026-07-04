#pragma once

#include <yui/core/ComponentContext.hpp>
#include <yui/core/Node.hpp>
#include <yui/core/VNode.hpp>
#include <yui/layout/Placement.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace yui::widgets {

// Tooltip: wraps a target and shows a floating tip panel near it after the
// pointer has hovered continuously for a delay (EventProps::onHoverDelay).
//
// Unlike Modal (a pure builder), a Tooltip is a COMPONENT: shown/position are
// state, set by the hover-delay callback and cleared on leave — so the
// builder converts to Child holding a Component (there is no single VNode to
// convert to, hence no operator VNode).
//
// Composition:
//
//   Box(target..., [Portal(tip)])    <- the wrapper carries onHoverDelay/onHover
//
// The tip's Portal is declared INSIDE the target wrapper on purpose: portal
// content hover-walks through its logical parent, so the wrapper is the LCA
// of any target<->tip pointer travel — its onHover(false) never fires
// mid-travel and the tip doesn't flicker away. Leaving BOTH the target and
// the tip walks the wrapper out of the hover chain and hides it.
//
// Placement is computed in the hover-delay callback, not during render:
// element refs read null in the render phase and settle in handlers, where
// the wrapper's drawn rect anchors placePanel just below it, clamped against
// the render root's rect as the viewport.

// The tip's top edge sits FLUSH with the target's bottom edge (gap 0). A
// nonzero gap opens a dead sample zone between the two rects: a slow pointer
// drag from target to tip lands a move sample IN the gap, over neither rect —
// the wrapper leaves the hover chain and the tip unmounts (and can't re-show,
// the tip carrying no onHoverDelay). Flush placement removes the zone.
// placePanel only ever anchors below and shifts up on overflow (never a
// separate flip-above branch), so this single below-anchor is symmetric.
inline constexpr float kTooltipGap = 0.0f;

// Chrome for the .tip("text") convenience panel.
inline constexpr uint32_t kTooltipBg = 0x303030FFu;
inline constexpr float kTooltipPad = 6.0f;
inline constexpr float kTooltipRadius = 4.0f;

class TooltipBuilder {
public:
    explicit TooltipBuilder(std::vector<Child> target) : target_(std::move(target)) {}

    // Tip content: any subtree, shown inside the positioned portal panel.
    TooltipBuilder& tip(Child content) {
        tip_ = std::move(content);
        return *this;
    }

    // Convenience: a default-styled one-line text tip.
    TooltipBuilder& tip(std::string message) {
        tip_ =
            Box(Text(std::move(message))).backgroundColor(kTooltipBg).padding(kTooltipPad).borderRadius(kTooltipRadius);
        return *this;
    }

    // Continuous-hover time (ms) before the tip shows. Unset =
    // render_defaults::kHoverDelayMs.
    TooltipBuilder& delayMs(float ms) {
        delayMs_ = ms;
        return *this;
    }

    // The tip panel's size, if known, so placement can shift the panel fully
    // on-screen near the viewport edges. Unset (zero) placement clamps the
    // anchor point alone — fine for tips well inside the window.
    TooltipBuilder& tipSize(float w, float h) {
        tipSize_ = layout::Vec{w, h};
        return *this;
    }

    // --- Conversion seam (see class note: Child-only, as a Component) ---
    operator Child() const& { return Child{build(target_, tip_)}; }
    operator Child() && { return Child{build(std::move(target_), std::move(tip_))}; }

private:
    // Tooltip state: visibility + the placed tip position (root space). One
    // slot — shown and position always change together.
    struct Placement {
        bool shown = false;
        float x = 0;
        float y = 0;
    };

    Component build(std::vector<Child> target, std::optional<Child> tip) const {
        return Component([target = std::move(target), tip = std::move(tip), delay = delayMs_,
                          tipSize = tipSize_](ComponentContext& ctx) -> VNode {
                   auto [placement, setPlacement] = ctx.useState<Placement>({});
                   NodeRef anchor = ctx.useElementRef();

                   // The hover delay elapsed: read the wrapper's drawn rect
                   // (handlers see settled layout) and place the tip just
                   // below it, clamped to the render root as the viewport.
                   auto show = [anchor, setPlacement, tipSize] {
                       Node* node = anchor.current();
                       if (!node)
                           return;
                       layout::Rect a = anchor.getBoundingRect();
                       const Node* root = node;
                       while (root->parent)
                           root = root->parent;
                       auto vp = layout::Viewport::uniform(root->layout.width, root->layout.height);
                       auto placed = layout::placePanel({a.x, a.bottom() + kTooltipGap}, tipSize, vp);
                       setPlacement({true, placed.x, placed.y});
                   };
                   auto hide = [setPlacement](bool entered) {
                       if (!entered)
                           setPlacement({});
                   };

                   std::vector<Child> kids = target;  // repeatable render: copy the wrapped subtree
                   if (placement.shown && tip) {
                       // A ZERO-SIZE anchor at the placed point, not a sized
                       // panel: portal content roots lay out against the
                       // viewport, so an unsized positioned wrapper would
                       // fill it — hovering/clicking anywhere would land on
                       // the tip's wrapper (leave-to-hide would never fire,
                       // content behind would be shadowed). The 0x0 anchor is
                       // never itself hittable; the tip child overflows it
                       // and is hit through subtree bounds. FlexStart keeps
                       // the cross-axis stretch from collapsing an unsized
                       // tip to the anchor's zero width.
                       kids.push_back(Portal(Box(*tip)
                                                 .width(0)
                                                 .height(0)
                                                 .alignItems(AlignItems::FlexStart)
                                                 .positionType(PositionType::Absolute)
                                                 .positionLeft(placement.x)
                                                 .positionTop(placement.y)));
                   }

                   auto wrapper = Box(std::move(kids)).ref(anchor).onHoverDelay(show).onHover(hide);
                   if (delay)
                       wrapper.hoverDelayMs(*delay);
                   return wrapper;  // implicit move: the && conversion to VNode
               })
            .setName("Tooltip");
    }

    std::vector<Child> target_;
    std::optional<Child> tip_;
    std::optional<float> delayMs_;
    layout::Vec tipSize_{0, 0};
};

// --- Factory functions (the Box/Portal/Modal factory idiom) ---

// Tooltip around a target (dynamic path: a runtime-built vector).
[[nodiscard]] inline TooltipBuilder Tooltip(std::vector<Child> target) {
    return TooltipBuilder{std::move(target)};
}

// Static path: each argument forwarded into a Child slot (see Box variadic).
template <class... Cs, typename = std::enable_if_t<!is_single_child_vector_v<Cs...>>>
[[nodiscard]] inline TooltipBuilder Tooltip(Cs&&... cs) {
    std::vector<Child> v;
    v.reserve(sizeof...(Cs));
    (v.emplace_back(std::forward<Cs>(cs)), ...);
    return TooltipBuilder{std::move(v)};
}

}  // namespace yui::widgets
