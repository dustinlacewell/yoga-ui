#include <yui/core/Measure.hpp>
#include <yui/render/TextWrap.hpp>

#include <cmath>
#include <string_view>

namespace yui {
namespace {

// Yoga rounds layout edges to the pixel grid, so a node measured at a
// fractional width can land with a content box narrower than the text it was
// sized for — and paint, which re-wraps at the FINAL content width, would
// spuriously break a run that fit at measure time. Integer sizes survive edge
// rounding exactly (both edges share one fractional part and round the same
// way), so ceiling here keeps measure and paint agreeing through the grid.
Size ceilToPixelGrid(Size s) {
    return {std::ceil(s.width), std::ceil(s.height)};
}

}  // namespace

// The single point where measure and paint agree: sizing wraps with the same
// algorithm the tree renderer paints with, over the backend's own run/metrics
// primitives.
Size ITextMeasurer::measure(const std::string& text, float fontSize, float maxWidth, std::string_view font) const {
    auto runs =
        render::wrapText(text, maxWidth, [&](std::string_view run) { return measureRun(run, fontSize, font); });
    return ceilToPixelGrid(render::runsSize(runs, fontMetrics(fontSize, font).lineHeight));
}

Size fallbackMeasure(const std::string& text, float fontSize, float maxWidth, std::string_view font) {
    auto runs = render::wrapText(text, maxWidth,
                                 [&](std::string_view run) { return fallbackMeasureRun(run, fontSize, font); });
    return ceilToPixelGrid(render::runsSize(runs, fallbackFontMetrics(fontSize, font).lineHeight));
}

}  // namespace yui
