#pragma once

#include "../core/Props.hpp"

#include <cstdint>
#include <optional>

namespace yui::render {

// Pure style cascade shared by every backend: base props < hoverStyle <
// focusStyle, each state override contributing only the fields it sets. This
// replaces the per-backend hand-mirrored copies that had to be kept in sync by
// eye; a backend never resolves a style itself.

// Box/Scroll chrome. backgroundColor/borderColor stay optional because absence
// means "don't draw"; width/radius resolve to floats (unset => 0 => not drawn /
// square corners).
struct ResolvedBoxStyle {
    std::optional<uint32_t> backgroundColor;
    std::optional<uint32_t> borderColor;
    float borderWidth = 0;
    float borderRadius = 0;
};

struct ResolvedTextStyle {
    float fontSize = 0;
    uint32_t color = 0;
};

// Input chrome is never optional: unset fields resolve to the shared
// render_defaults::kInput* values, and the default hover/focus border rule
// (kInputHoverBorder / kInputFocusBorder unless the user set a base
// borderColor) is applied here.
struct ResolvedInputStyle {
    uint32_t backgroundColor = 0;
    uint32_t borderColor = 0;
    float borderWidth = 0;
    float borderRadius = 0;
    float fontSize = 0;
    uint32_t color = 0;
};

ResolvedBoxStyle resolveBox(const BoxProps& p, bool hovered, bool focused);
ResolvedBoxStyle resolveBox(const ScrollProps& p, bool hovered, bool focused);
ResolvedTextStyle resolveText(const TextProps& p, bool hovered, bool focused);
ResolvedInputStyle resolveInput(const InputProps& p, bool hovered, bool focused);

}  // namespace yui::render
