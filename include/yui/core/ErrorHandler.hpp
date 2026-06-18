#pragma once

#include <exception>
#include <functional>
#include <string_view>

namespace yui {

// Diagnostic sink for exceptions escaping user callbacks. yui isolates a throwing
// callback, keeps its own invariants intact, and reports the failure here rather
// than letting it escape into the platform (e.g. the host DAW) or corrupt the
// tree.
//
//   where     — a stable label for the call site ("rerenderComponent", "onClick",
//               "effect", "draw", …) so the consumer can locate the offender.
//   eOrNull   — the caught std::exception, or nullptr when the throw was not a
//               std::exception (a catch(...) case).
//
// The handler itself must not throw; it is invoked from contexts that are
// already unwinding a failed callback, including destructors.
using ErrorHandler = std::function<void(std::string_view where, const std::exception* eOrNull)>;

}  // namespace yui
