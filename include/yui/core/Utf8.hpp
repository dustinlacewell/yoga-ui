#pragma once

#include <cstddef>
#include <string_view>

namespace yui::utf8 {

// UTF-8 code-point boundary helpers shared by text editing (caret movement in
// EventHandler), controlled-value clamping (InputNode::updateProps), and
// display-prefix masking (TreeRenderer). All use the continuation-byte scan
// ((b & 0xC0) == 0x80): a byte starts a code point iff it is not a
// continuation byte. Offsets are byte offsets; every function returns a valid
// boundary of `text` in [0, size].

// Boundary of the code point BEFORE offset `i` (0 stays 0). `i` is expected to
// be a boundary itself; a non-boundary input snaps past the whole enclosing
// code point, which is still a valid boundary.
inline size_t prevCodePoint(std::string_view text, size_t i) {
    if (i == 0)
        return 0;
    --i;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
        --i;
    return i;
}

// Boundary just past the code point AT offset `i` (size stays size).
inline size_t nextCodePoint(std::string_view text, size_t i) {
    if (i >= text.size())
        return text.size();
    ++i;
    while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
        ++i;
    return i;
}

// Clamp `i` into [0, size] and snap BACKWARD to the nearest boundary — the
// normalization applied when external text replaces a value out from under a
// caret (a caret must never sit mid-code-point).
inline size_t snapToCodePoint(std::string_view text, size_t i) {
    if (i >= text.size())
        return text.size();
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
        --i;
    return i;
}

// Number of code points in `text`: count the non-continuation bytes.
inline size_t codePointCount(std::string_view text) {
    size_t count = 0;
    for (char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
            ++count;
    }
    return count;
}

}  // namespace yui::utf8
