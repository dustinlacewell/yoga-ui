#pragma once

#include <functional>
#include <string>

namespace yui {

// Size returned by measure functions
struct Size {
    float width = 0;
    float height = 0;
};

// Text measurement function signature
// Parameters: text, fontSize, maxWidth (for wrapping, 0 = no limit)
// Returns: measured size
using TextMeasureFunc = std::function<Size(const std::string& text, float fontSize, float maxWidth)>;

// Global measure function registry
// Backends set this; core uses it for intrinsic sizing
class Measure {
public:
    static void setTextMeasure(TextMeasureFunc func) { textMeasure_ = std::move(func); }

    static Size measureText(const std::string& text, float fontSize, float maxWidth = 0) {
        if (textMeasure_) {
            return textMeasure_(text, fontSize, maxWidth);
        }
        // Fallback: rough estimate if no backend registered
        // Assumes ~0.6 * fontSize per character width, fontSize for height
        float charWidth = fontSize * 0.6f;
        float width = text.length() * charWidth;
        if (maxWidth > 0 && width > maxWidth) {
            width = maxWidth;
        }
        return {width, fontSize};
    }

private:
    static inline TextMeasureFunc textMeasure_;
};

}  // namespace yui
