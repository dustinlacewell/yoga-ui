#pragma once

#include "../core/Node.hpp"

#include <string>
#include <unordered_map>

#include <SDL.h>
#include <SDL_ttf.h>

namespace yui {
namespace sdl {

// SDL2 + SDL2_ttf + SDL2_gfx renderer for yui
class SdlRenderer {
public:
    SdlRenderer(SDL_Renderer* renderer, const std::string& fontPath, int baseFontSize = 16);
    ~SdlRenderer();

    // Render a node tree
    void render(Node* root);

    // Register text measure function with yui::Measure
    void registerMeasureFunc();

private:
    void renderNode(Node* node, float offsetX, float offsetY);
    void renderBox(BoxNode* node, float x, float y);
    void renderText(TextNode* node, float x, float y);
    void renderInput(InputNode* node, float x, float y);
    void renderScroll(ScrollNode* node, float offsetX, float offsetY);
    void renderCanvas(CanvasNode* node, float x, float y);

    // Draw a rounded rectangle (uses SDL2_gfx)
    void drawRoundedRect(float x, float y, float w, float h, float radius, uint32_t color, bool filled);

    // Draw text at native font size (no scaling)
    void drawText(const std::string& text, float x, float y, float fontSize, uint32_t color);

    // Get or create a font at the specified size
    TTF_Font* getFont(int size);

    SDL_Renderer* renderer_;
    std::string fontPath_;
    int baseFontSize_;
    std::unordered_map<int, TTF_Font*> fontCache_;
};

// Initialize SDL2 and SDL2_ttf (call once at startup)
bool initSDL();

// Cleanup SDL2 and SDL2_ttf (call at shutdown)
void quitSDL();

}  // namespace sdl
}  // namespace yui
