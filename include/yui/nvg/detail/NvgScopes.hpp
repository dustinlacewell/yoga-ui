#pragma once

#include <nanovg.h>

namespace yui {
namespace nvg {
namespace detail {

// RAII pairing of nvgSave / nvgRestore. Saves the NanoVG render state on
// construction and restores it on destruction, so the state is balanced on any
// scope exit — normal fall-through, early return, or an exception thrown by a
// nested draw call.
class NvgSaveScope {
public:
    explicit NvgSaveScope(NVGcontext* vg) : vg_(vg) { nvgSave(vg_); }
    ~NvgSaveScope() { nvgRestore(vg_); }

    NvgSaveScope(const NvgSaveScope&) = delete;
    NvgSaveScope& operator=(const NvgSaveScope&) = delete;

private:
    NVGcontext* vg_;
};

}  // namespace detail
}  // namespace nvg
}  // namespace yui
