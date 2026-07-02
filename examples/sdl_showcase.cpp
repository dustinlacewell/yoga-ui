// SDL Showcase v2 for yui
// Demonstrates yui with clean component architecture and theming
//
// Build: make sdl_showcase
// Run: ./sdl_showcase

#define _USE_MATH_DEFINES
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "showcase/showcase.hpp"
#include "yui/sdl/sdl.hpp"

#include <iostream>

#include <SDL.h>
#include <SDL_ttf.h>

using namespace yui;
using namespace showcase;

// ============================================================================
// Canvas Demo - SDL-specific custom drawing
// ============================================================================

Component CanvasDemo() {
    return [](ComponentContext&) -> VNode {
        const auto& t = theme();

        return Section("Canvas", {
                                     Box(Canvas([](void* ctx, float w, float h) {
                                             auto* r = static_cast<SDL_Renderer*>(ctx);
                                             float cx = w / 2;
                                             float cy = h / 2;
                                             float radius = std::min(w, h) / 2 - 4;

                                             // Circle
                                             SDL_SetRenderDrawColor(r, 0xFF, 0xD7, 0x00, 0xFF);
                                             for (int dy = -radius; dy <= radius; dy++) {
                                                 float dx = std::sqrt(radius * radius - dy * dy);
                                                 SDL_RenderDrawLineF(r, cx - dx, cy + dy, cx + dx, cy + dy);
                                             }

                                             // Eyes
                                             SDL_SetRenderDrawColor(r, 0x00, 0x00, 0x00, 0xFF);
                                             float eyeR = radius * 0.12f;
                                             float eyeY = cy - radius * 0.25f;
                                             for (float ex : {cx - radius * 0.3f, cx + radius * 0.3f}) {
                                                 for (int dy = -eyeR; dy <= eyeR; dy++) {
                                                     float dx = std::sqrt(eyeR * eyeR - dy * dy);
                                                     SDL_RenderDrawLineF(r, ex - dx, eyeY + dy, ex + dx, eyeY + dy);
                                                 }
                                             }

                                             // Smile
                                             float smileR = radius * 0.5f;
                                             float smileY = cy + radius * 0.1f;
                                             for (int i = 0; i < 60; i++) {
                                                 float angle = (30 + i) * M_PI / 180.0f;
                                                 float px = cx + smileR * std::cos(angle);
                                                 float py = smileY + smileR * std::sin(angle);
                                                 SDL_RenderDrawPointF(r, px, py);
                                                 SDL_RenderDrawPointF(r, px, py + 1);
                                             }
                                         })
                                             .width(80)
                                             .height(80))
                                         .backgroundColor(t.bg)
                                         .borderRadius(t.radiusSm)
                                         .padding(t.gap),

                                     Label("Custom SDL2 drawing"),
                                 });
    };
}

// ============================================================================
// SDL Host
// ============================================================================

VNode buildUI() {
    return Box(App("YUI Showcase", "SDL2", CanvasDemo())).flexGrow(1);
}

class SdlHost : public Host {
public:
    SdlHost(SDL_Renderer* renderer, const std::string& fontPath) : renderer_(renderer, fontPath, 14) {
        setTextMeasurer(&renderer_);
        setRender(buildUI);
    }

    void frame(int width, int height, float dt) {
        update(width, height, dt);
        renderer_.render(root());
    }

    sdl::SdlRenderer renderer_;
};

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    if (!sdl::initSDL()) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("yui Showcase v2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1000, 700,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "Failed to create window\n";
        sdl::quitSDL();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "Failed to create renderer\n";
        SDL_DestroyWindow(window);
        sdl::quitSDL();
        return 1;
    }

    // Find font - try common paths for each OS
    std::string fontPath;
    const char* fontPaths[] = {// Windows
                               "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                               // Linux
                               "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                               "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                               // macOS
                               "/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/SFNS.ttf",
                               "/Library/Fonts/Arial.ttf", nullptr};
    for (const char** path = fontPaths; *path; path++) {
        TTF_Font* testFont = TTF_OpenFont(*path, 16);
        if (testFont) {
            fontPath = *path;
            TTF_CloseFont(testFont);
            break;
        }
    }
    if (fontPath.empty()) {
        std::cerr << "Failed to find font\n";
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        sdl::quitSDL();
        return 1;
    }

    {
        Store<AppState> store;
        storePtr = &store;

        SdlHost host(renderer, fontPath);
        SDL_StartTextInput();

        bool running = true;
        SDL_Event event;

        while (running) {
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    host.handleMouseDown(
                        event.button.x, event.button.y,
                        event.button.button == SDL_BUTTON_RIGHT    ? MouseButton::Right
                        : event.button.button == SDL_BUTTON_MIDDLE ? MouseButton::Middle
                                                                   : MouseButton::Left);
                    break;

                case SDL_MOUSEBUTTONUP:
                    host.handleMouseUp(
                        event.button.x, event.button.y,
                        event.button.button == SDL_BUTTON_RIGHT    ? MouseButton::Right
                        : event.button.button == SDL_BUTTON_MIDDLE ? MouseButton::Middle
                                                                   : MouseButton::Left);
                    break;

                case SDL_MOUSEMOTION:
                    host.handleMouseMove(event.motion.x, event.motion.y);
                    break;

                case SDL_MOUSEWHEEL:
                    host.handleScroll(event.wheel.mouseX, event.wheel.mouseY, event.wheel.x * 20, event.wheel.y * 20);
                    break;

                case SDL_TEXTINPUT:
                    host.handleTextInput(event.text.text);
                    break;

                case SDL_KEYDOWN: {
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        running = false;
                    } else if (event.key.keysym.sym == SDLK_BACKSPACE) {
                        host.handleBackspace();
                    } else if (event.key.keysym.sym == SDLK_RETURN) {
                        host.handleSubmit();
                    }
                    // Convert SDL modifiers to yui KeyMod
                    uint16_t mods = 0;
                    if (event.key.keysym.mod & KMOD_SHIFT)
                        mods |= KeyMod_Shift;
                    if (event.key.keysym.mod & KMOD_CTRL)
                        mods |= KeyMod_Ctrl;
                    if (event.key.keysym.mod & KMOD_ALT)
                        mods |= KeyMod_Alt;
                    if (event.key.keysym.mod & KMOD_GUI)
                        mods |= KeyMod_Super;
                    if (event.key.keysym.mod & KMOD_CAPS)
                        mods |= KeyMod_CapsLock;
                    if (event.key.keysym.mod & KMOD_NUM)
                        mods |= KeyMod_NumLock;
                    // Tab traversal lives in the platform shim (core stays
                    // keycode-agnostic; SDL's Tab is 9), and only when no app
                    // handler consumed the key.
                    bool consumed =
                        host.handleKeyDown(event.key.keysym.sym, mods, event.key.repeat != 0);
                    if (!consumed && event.key.keysym.sym == SDLK_TAB) {
                        if (mods & KeyMod_Shift)
                            host.focusPrev();
                        else
                            host.focusNext();
                    }
                    break;
                }

                case SDL_KEYUP: {
                    uint16_t mods = 0;
                    if (event.key.keysym.mod & KMOD_SHIFT)
                        mods |= KeyMod_Shift;
                    if (event.key.keysym.mod & KMOD_CTRL)
                        mods |= KeyMod_Ctrl;
                    if (event.key.keysym.mod & KMOD_ALT)
                        mods |= KeyMod_Alt;
                    if (event.key.keysym.mod & KMOD_GUI)
                        mods |= KeyMod_Super;
                    if (event.key.keysym.mod & KMOD_CAPS)
                        mods |= KeyMod_CapsLock;
                    if (event.key.keysym.mod & KMOD_NUM)
                        mods |= KeyMod_NumLock;
                    host.handleKeyUp(event.key.keysym.sym, mods);
                    break;
                }
                }
            }

            int width, height;
            SDL_GetWindowSize(window, &width, &height);

            SDL_SetRenderDrawColor(renderer, 0x0a, 0x0a, 0x0a, 0xFF);
            SDL_RenderClear(renderer);
            host.frame(width, height, 0.016f);
            SDL_RenderPresent(renderer);
        }

        SDL_StopTextInput();
        storePtr = nullptr;
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    sdl::quitSDL();
    return 0;
}
