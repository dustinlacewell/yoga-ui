#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

namespace yui::detail {

// Transparent hash for std::string-keyed maps so per-frame lookups can pass a
// std::string_view without materializing a temporary std::string. Pair with
// std::equal_to<> as the key-equality functor: is_transparent on BOTH functors
// is what enables the heterogeneous find() overloads (C++20).
struct TransparentStringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

}  // namespace yui::detail
