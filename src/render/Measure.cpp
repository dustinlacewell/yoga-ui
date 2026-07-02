#include <yui/core/Measure.hpp>
#include <yui/render/TextWrap.hpp>

#include <string_view>

namespace yui {

// The single point where measure and paint agree: sizing wraps with the same
// algorithm the tree renderer paints with, over the backend's own run/metrics
// primitives.
Size ITextMeasurer::measure(const std::string& text, float fontSize, float maxWidth, const std::string& font) const {
    auto runs =
        render::wrapText(text, maxWidth, [&](std::string_view run) { return measureRun(run, fontSize, font); });
    return render::runsSize(runs, fontMetrics(fontSize, font).lineHeight);
}

Size fallbackMeasure(const std::string& text, float fontSize, float maxWidth, const std::string& font) {
    auto runs = render::wrapText(text, maxWidth,
                                 [&](std::string_view run) { return fallbackMeasureRun(run, fontSize, font); });
    return render::runsSize(runs, fallbackFontMetrics(fontSize, font).lineHeight);
}

}  // namespace yui
