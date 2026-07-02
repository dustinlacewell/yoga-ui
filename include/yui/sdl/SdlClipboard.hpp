#pragma once

#include "../core/Clipboard.hpp"

#include <string>

#include <SDL.h>

namespace yui {
namespace sdl {

// SDL2 system clipboard: the IClipboard seam over SDL_GetClipboardText /
// SDL_SetClipboardText. SDL's clipboard API is windowless (only video init is
// required), so one instance serves any number of hosts.
class SdlClipboard : public IClipboard {
public:
    std::string getText() override {
        // SDL_GetClipboardText returns a malloc'd buffer the CALLER must
        // SDL_free — copy into a std::string, then release. SDL documents ""
        // (never null) on empty/error, but guard anyway.
        char* raw = SDL_GetClipboardText();
        if (!raw)
            return {};
        std::string text(raw);
        SDL_free(raw);
        return text;
    }

    void setText(const std::string& text) override { SDL_SetClipboardText(text.c_str()); }
};

}  // namespace sdl
}  // namespace yui
