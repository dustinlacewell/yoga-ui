#include "doctest.h"

#include <yui/detail/TransparentStringHash.hpp>
#include <yui/sdl/detail/GfxClamp.hpp>

#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

using yui::sdl::detail::clampToGfxCoord;

// clampToGfxCoord guards the float -> Sint16 casts in front of the SDL2_gfx
// primitives: it must saturate (never wrap) and truncate in-range values
// exactly like the cast it replaced.

TEST_CASE("clampToGfxCoord: in-range values truncate like a plain cast") {
    CHECK(clampToGfxCoord(0.0f) == 0);
    CHECK(clampToGfxCoord(123.9f) == 123);
    CHECK(clampToGfxCoord(-123.9f) == -123);
    CHECK(clampToGfxCoord(32766.9f) == 32766);
    CHECK(clampToGfxCoord(-32767.9f) == -32767);
}

TEST_CASE("clampToGfxCoord: saturates at the Sint16 boundaries instead of wrapping") {
    CHECK(clampToGfxCoord(32767.0f) == 32767);
    CHECK(clampToGfxCoord(-32768.0f) == -32768);
    CHECK(clampToGfxCoord(32768.0f) == 32767);
    CHECK(clampToGfxCoord(-32769.0f) == -32768);
    CHECK(clampToGfxCoord(100000.0f) == 32767);
    CHECK(clampToGfxCoord(-100000.0f) == -32768);
    CHECK(clampToGfxCoord(1.0e9f) == 32767);
    CHECK(clampToGfxCoord(-1.0e9f) == -32768);
}

TEST_CASE("clampToGfxCoord: non-finite inputs stay defined") {
    CHECK(clampToGfxCoord(std::numeric_limits<float>::infinity()) == 32767);
    CHECK(clampToGfxCoord(-std::numeric_limits<float>::infinity()) == -32768);
    CHECK(clampToGfxCoord(std::numeric_limits<float>::quiet_NaN()) == 0);
}

// The backends' font registries key on std::string but are queried with a
// string_view on the per-frame paths; the transparent hash + std::equal_to<>
// pair is what lets find(string_view) hit the string keys without allocating.
TEST_CASE("TransparentStringHash: string_view lookups find string keys") {
    std::unordered_map<std::string, int, yui::detail::TransparentStringHash, std::equal_to<>> map;
    map[std::string{}] = 1;
    map["mono"] = 2;

    // The empty view is the "default font" name both backends fall back to.
    REQUIRE(map.find(std::string_view{}) != map.end());
    CHECK(map.find(std::string_view{})->second == 1);
    REQUIRE(map.find(std::string_view{"mono"}) != map.end());
    CHECK(map.find(std::string_view{"mono"})->second == 2);
    CHECK(map.find(std::string_view{"missing"}) == map.end());
}
