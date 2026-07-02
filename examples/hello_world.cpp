// Minimal "Hello World" example for yui
//
// Build: cmake --build build --target hello_world
// Run:   ./build/bin/hello_world

#include "yui/yui.hpp"
#include "yui/sdl/sdl.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <iostream>

using namespace yui;

// UI: centered "Hello, World!" text
VNode buildUI() {
    return Box(Text("Hello, World!").fontSize(32).color(0xFFFFFFFF))
        .flexGrow(1)
        .justifyContent(JustifyContent::Center)
        .alignItems(AlignItems::Center)
        .backgroundColor(0x1a1a2eFF);
}

class HelloHost : public Host {
public:
    HelloHost(SDL_Renderer* renderer, const std::string& fontPath) : renderer_(renderer, fontPath, 16) {
        setTextMeasurer(&renderer_);
        setRender(buildUI);
    }

    void frame(int w, int h) {
        update(w, h, 1.0f / 60.0f);
        renderer_.render(root());
    }

private:
    sdl::SdlRenderer renderer_;
};

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    if (!sdl::initSDL()) {
        std::cerr << "Failed to initialize SDL\n";
        return 1;
    }

    SDL_Window* window =
        SDL_CreateWindow("Hello yui", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 400, 300, SDL_WINDOW_SHOWN);
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

    // Find a system font
    std::string fontPath;
    const char* fontPaths[] = {"C:/Windows/Fonts/segoeui.ttf",
                               "C:/Windows/Fonts/arial.ttf",
                               "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                               "/System/Library/Fonts/Helvetica.ttc",
                               nullptr};
    for (const char** p = fontPaths; *p; p++) {
        if (TTF_Font* f = TTF_OpenFont(*p, 16)) {
            fontPath = *p;
            TTF_CloseFont(f);
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

    HelloHost host(renderer, fontPath);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }

        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        host.frame(w, h);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    sdl::quitSDL();
    return 0;
}
