#include <yui/render/TextWrap.hpp>

#include <algorithm>

namespace yui::render {
namespace {

constexpr float kUnmeasured = -1.0f;

// End of the UTF-8 code point starting at `i`: skip its continuation bytes
// (0b10xxxxxx) — the same scan the backspace edit in EventHandler uses.
size_t codePointEnd(std::string_view text, size_t i, size_t limit) {
    ++i;
    while (i < limit && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) {
        ++i;
    }
    return i;
}

// The line currently being filled. `end` only ever advances onto placed
// content, so trailing spaces never extend a line's byte range or width.
struct Line {
    size_t begin = 0;
    size_t end = 0;
    float width = 0;
    bool hasToken = false;
};

// Width of one inter-token gap of `count` spaces; the single-space advance is
// measured once per wrapText call (advance-additive, like token widths).
float gapWidth(size_t count, float& spaceWidth, const MeasureRunFn& measureRun) {
    if (count == 0) {
        return 0;
    }
    if (spaceWidth == kUnmeasured) {
        spaceWidth = measureRun(" ");
    }
    return static_cast<float>(count) * spaceWidth;
}

// A token wider than maxWidth alone on a line: emit code-point-sized pieces,
// each as wide as fits and never empty (so progress is guaranteed even when a
// single code point overflows). The final piece stays as the open line so
// following tokens can join it.
void breakOverlongToken(std::string_view text, size_t tokBegin, size_t tokEnd, float maxWidth,
                        const MeasureRunFn& measureRun, Line& line, std::vector<TextRun>& out) {
    for (size_t cp = tokBegin; cp < tokEnd;) {
        size_t cpEnd = codePointEnd(text, cp, tokEnd);
        float cpW = measureRun(text.substr(cp, cpEnd - cp));
        if (cp > line.begin && line.width + cpW > maxWidth) {
            out.push_back({line.begin, cp, line.width});
            line = {cp, cp, 0, false};
        }
        line.width += cpW;
        line.end = cpEnd;
        cp = cpEnd;
    }
    line.hasToken = true;
}

// Greedy word wrap of one hard segment [begin, end) — no newlines inside.
// Always emits at least one run (an empty segment is one empty run).
void wrapSegment(std::string_view text, size_t begin, size_t end, float maxWidth, const MeasureRunFn& measureRun,
                 float& spaceWidth, std::vector<TextRun>& out) {
    if (maxWidth <= 0) {
        out.push_back({begin, end, measureRun(text.substr(begin, end - begin))});
        return;
    }

    Line line{begin, begin, 0, false};
    size_t i = begin;
    while (i < end) {
        size_t spacesBegin = i;
        while (i < end && text[i] == ' ') {
            ++i;
        }
        if (i == end) {
            break;  // trailing spaces: a line ends at its last token
        }
        size_t tokBegin = i;
        while (i < end && text[i] != ' ') {
            ++i;
        }
        size_t tokEnd = i;

        float gap = gapWidth(tokBegin - spacesBegin, spaceWidth, measureRun);
        float tokW = measureRun(text.substr(tokBegin, tokEnd - tokBegin));

        if (line.hasToken && line.width + gap + tokW > maxWidth) {
            // Break BEFORE the token; the break-point spaces are dropped.
            out.push_back({line.begin, line.end, line.width});
            line = {tokBegin, tokBegin, 0, false};
            gap = 0;
        }
        if (!line.hasToken && line.width + gap + tokW > maxWidth) {
            // Nothing to break before — force-break the token itself. Leading
            // segment spaces (if any) ride the first piece.
            line.width += gap;
            breakOverlongToken(text, tokBegin, tokEnd, maxWidth, measureRun, line, out);
            continue;
        }
        line.width += gap + tokW;
        line.end = tokEnd;
        line.hasToken = true;
    }
    out.push_back({line.begin, line.end, line.width});
}

}  // namespace

std::vector<TextRun> wrapText(std::string_view text, float maxWidth, const MeasureRunFn& measureRun) {
    std::vector<TextRun> runs;
    float spaceWidth = kUnmeasured;
    size_t segBegin = 0;
    for (;;) {
        size_t nl = text.find('\n', segBegin);
        size_t segEnd = (nl == std::string_view::npos) ? text.size() : nl;
        wrapSegment(text, segBegin, segEnd, maxWidth, measureRun, spaceWidth, runs);
        if (nl == std::string_view::npos) {
            break;
        }
        segBegin = nl + 1;
    }
    return runs;
}

Size runsSize(const std::vector<TextRun>& runs, float lineHeight) {
    float width = 0;
    for (const TextRun& run : runs) {
        width = std::max(width, run.width);
    }
    return {width, static_cast<float>(runs.size()) * lineHeight};
}

}  // namespace yui::render
