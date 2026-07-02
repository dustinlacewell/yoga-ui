#pragma once

#include "../core/Measure.hpp"

#include <cstddef>
#include <functional>
#include <string_view>
#include <vector>

namespace yui::render {

// One wrapped line: a [begin, end) byte range into the source text plus its
// measured advance width. Ranges never split a UTF-8 code point.
struct TextRun {
    size_t begin = 0;
    size_t end = 0;
    float width = 0;
};

// Advance width of ONE newline-free run at the target font/size (the caller
// binds font and size; see ITextMeasurer::measureRun).
using MeasureRunFn = std::function<float(std::string_view run)>;

// THE wrapping algorithm — the single one measure and paint share, so a text
// block lays out at exactly the lines it draws.
//
//   - '\n' always breaks (a trailing '\n' yields a final empty run; empty text
//     yields one empty run).
//   - maxWidth <= 0 disables soft wrapping: each hard segment is one run.
//   - Otherwise greedy word wrap: break BEFORE a token that would overflow a
//     non-empty line; the spaces at a break point are dropped (neither counted
//     at the line end nor carried to the next line's start).
//   - Run widths are advance-additive: each token (and the single-space
//     advance) is measured once and summed, never re-measured per line.
//   - A token wider than maxWidth alone on a line breaks at UTF-8 code-point
//     boundaries, at least one code point per line.
std::vector<TextRun> wrapText(std::string_view text, float maxWidth, const MeasureRunFn& measureRun);

// Size of a wrapped block: widest run x (#runs * lineHeight).
Size runsSize(const std::vector<TextRun>& runs, float lineHeight);

}  // namespace yui::render
