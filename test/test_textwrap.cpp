#include "doctest.h"

#include <yui/render/TextWrap.hpp>

#include <string>
#include <string_view>
#include <vector>

using yui::render::TextRun;
using yui::render::runsSize;
using yui::render::wrapText;

namespace {

// 10px per byte, and counts calls so tests can pin "each token measured once".
struct CountingMeasure {
    mutable int calls = 0;
    float operator()(std::string_view run) const {
        ++calls;
        return static_cast<float>(run.size()) * 10.0f;
    }
};

void checkRun(const TextRun& run, size_t begin, size_t end, float width) {
    CHECK(run.begin == begin);
    CHECK(run.end == end);
    CHECK(run.width == doctest::Approx(width));
}

}  // namespace

// ---------------------------------------------------------------------------
// Soft wrapping: greedy word wrap, advance-additive widths.
// ---------------------------------------------------------------------------

TEST_CASE("wrapText: a line that fits stays one run") {
    CountingMeasure m;
    auto runs = wrapText("hello world", 200, std::ref(m));

    REQUIRE(runs.size() == 1);
    checkRun(runs[0], 0, 11, 110);
}

TEST_CASE("wrapText: greedy break at a word boundary, each token measured once") {
    CountingMeasure m;
    //          0123456789
    auto runs = wrapText("aaa bb ccc", 70, std::ref(m));

    REQUIRE(runs.size() == 2);
    checkRun(runs[0], 0, 6, 60);  // "aaa bb"
    checkRun(runs[1], 7, 10, 30);  // "ccc"
    // 3 tokens + the single space, each measured exactly once.
    CHECK(m.calls == 4);
}

TEST_CASE("wrapText: the space at a break point is dropped from both lines") {
    CountingMeasure m;
    auto runs = wrapText("aaa bb ccc", 70, std::ref(m));

    REQUIRE(runs.size() == 2);
    // Line 1 ends at "bb" (byte 6, before the break space) and its width
    // excludes that space; line 2 starts at "ccc" (byte 7, after it).
    CHECK(runs[0].end == 6);
    CHECK(runs[0].width == doctest::Approx(60));
    CHECK(runs[1].begin == 7);
}

TEST_CASE("wrapText: multiple interior spaces accumulate advance-additively") {
    CountingMeasure m;
    //          012345
    auto runs = wrapText("ab  cd", 100, std::ref(m));

    REQUIRE(runs.size() == 1);
    checkRun(runs[0], 0, 6, 60);  // 20 + 2*10 + 20
}

TEST_CASE("wrapText: token exactly maxWidth wide fits without breaking") {
    CountingMeasure m;
    auto runs = wrapText("abcd", 40, std::ref(m));
    REQUIRE(runs.size() == 1);
    checkRun(runs[0], 0, 4, 40);

    // ...including as the overflow token of a break: it lands alone, unsplit.
    //     0123456
    runs = wrapText("ab abcd", 40, std::ref(m));
    REQUIRE(runs.size() == 2);
    checkRun(runs[0], 0, 2, 20);  // "ab"
    checkRun(runs[1], 3, 7, 40);  // "abcd"
}

// ---------------------------------------------------------------------------
// Hard breaks: '\n' always splits, independent of maxWidth.
// ---------------------------------------------------------------------------

TEST_CASE("wrapText: newline breaks unconditionally") {
    CountingMeasure m;
    auto runs = wrapText("ab\ncd", 1000, std::ref(m));

    REQUIRE(runs.size() == 2);
    checkRun(runs[0], 0, 2, 20);
    checkRun(runs[1], 3, 5, 20);
}

TEST_CASE("wrapText: trailing newline yields a final empty run") {
    CountingMeasure m;
    auto runs = wrapText("ab\n", 1000, std::ref(m));

    REQUIRE(runs.size() == 2);
    checkRun(runs[0], 0, 2, 20);
    checkRun(runs[1], 3, 3, 0);
}

TEST_CASE("wrapText: consecutive newlines yield empty runs between them") {
    CountingMeasure m;
    auto runs = wrapText("\n\n", 1000, std::ref(m));

    REQUIRE(runs.size() == 3);
    checkRun(runs[0], 0, 0, 0);
    checkRun(runs[1], 1, 1, 0);
    checkRun(runs[2], 2, 2, 0);
}

TEST_CASE("wrapText: empty text is one empty run") {
    CountingMeasure m;
    auto runs = wrapText("", 100, std::ref(m));

    REQUIRE(runs.size() == 1);
    checkRun(runs[0], 0, 0, 0);
}

TEST_CASE("wrapText: maxWidth <= 0 disables soft wrap but newlines still break") {
    CountingMeasure m;
    //          0123456789
    auto runs = wrapText("aaa bbb\ncc", 0, std::ref(m));

    REQUIRE(runs.size() == 2);
    checkRun(runs[0], 0, 7, 70);  // whole segment, spaces and all
    checkRun(runs[1], 8, 10, 20);
}

// ---------------------------------------------------------------------------
// Over-long tokens: forced breaks at UTF-8 code-point boundaries.
// ---------------------------------------------------------------------------

TEST_CASE("wrapText: over-long token breaks at UTF-8 code-point boundaries") {
    CountingMeasure m;
    // Five 2-byte code points (20px each, 100px total) in a 50px line: pieces
    // of 2, 2, 1 code points. Every boundary must land on an even byte offset —
    // never inside a code point.
    std::string text = "ééééé";
    REQUIRE(text.size() == 10);

    auto runs = wrapText(text, 50, std::ref(m));

    REQUIRE(runs.size() == 3);
    checkRun(runs[0], 0, 4, 40);
    checkRun(runs[1], 4, 8, 40);
    checkRun(runs[2], 8, 10, 20);
}

TEST_CASE("wrapText: forced break always advances at least one code point") {
    CountingMeasure m;
    // Every character is wider (10px) than the 5px line; each still gets its
    // own run — no empty runs, no infinite loop.
    auto runs = wrapText("abc", 5, std::ref(m));

    REQUIRE(runs.size() == 3);
    checkRun(runs[0], 0, 1, 10);
    checkRun(runs[1], 1, 2, 10);
    checkRun(runs[2], 2, 3, 10);
}

TEST_CASE("wrapText: the tail of a forced break stays open for following tokens") {
    CountingMeasure m;
    // "aaaaa" force-breaks into "aaaa" + "a"; the trailing "a" (10px) then has
    // room for " bb" (10 + 20), so the second line is "a bb".
    //          01234567
    auto runs = wrapText("aaaaa bb", 40, std::ref(m));

    REQUIRE(runs.size() == 2);
    checkRun(runs[0], 0, 4, 40);  // "aaaa"
    checkRun(runs[1], 4, 8, 40);  // "a bb"
}

// ---------------------------------------------------------------------------
// runsSize: widest run x (#runs * lineHeight).
// ---------------------------------------------------------------------------

TEST_CASE("runsSize: widest run wide, one lineHeight per run tall") {
    CountingMeasure m;
    auto runs = wrapText("aaa bb ccc", 70, std::ref(m));

    auto size = runsSize(runs, 12.0f);
    CHECK(size.width == doctest::Approx(60));
    CHECK(size.height == doctest::Approx(24));

    auto empty = runsSize(wrapText("", 70, std::ref(m)), 12.0f);
    CHECK(empty.width == doctest::Approx(0));
    CHECK(empty.height == doctest::Approx(12));  // empty text is still one line
}
