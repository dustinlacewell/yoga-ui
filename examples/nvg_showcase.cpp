// NanoVG Showcase v2 for yui
// Demonstrates yui with clean component architecture and theming
//
// Build: make nvg_showcase
// Run: ./nvg_showcase

#define _USE_MATH_DEFINES
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "showcase/showcase.hpp"
#include "yui/nvg/nvg.hpp"

#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <nanovg.h>
#define NANOVG_GL3_IMPLEMENTATION
#include <iostream>

#include <nanovg_gl.h>

using namespace yui;
using namespace showcase;

// ============================================================================
// Canvas Demo - NanoVG-specific custom drawing
// ============================================================================

Component CanvasDemo() {
    return [](ComponentContext&) -> VNode {
        const auto& t = theme();

        return Section("Canvas", {
             Box(Canvas([](void* ctx, float w, float h) {
                     auto* vg = static_cast<NVGcontext*>(ctx);
                     float cx = w / 2;
                     float cy = h / 2;
                     float radius = std::min(w, h) / 2 - 4;

                     // Face
                     nvgBeginPath(vg);
                     nvgCircle(vg, cx, cy, radius);
                     nvgFillColor(vg, nvgRGB(0xFF, 0xD7, 0x00));
                     nvgFill(vg);

                     // Eyes
                     nvgFillColor(vg, nvgRGB(0x00, 0x00, 0x00));
                     float eyeR = radius * 0.12f;
                     float eyeY = cy - radius * 0.25f;

                     nvgBeginPath(vg);
                     nvgCircle(vg, cx - radius * 0.3f, eyeY, eyeR);
                     nvgFill(vg);

                     nvgBeginPath(vg);
                     nvgCircle(vg, cx + radius * 0.3f, eyeY, eyeR);
                     nvgFill(vg);

                     // Smile
                     nvgBeginPath(vg);
                     nvgArc(vg, cx, cy + radius * 0.1f, radius * 0.5f, 30.0f * M_PI / 180.0f,
                            150.0f * M_PI / 180.0f, NVG_CW);
                     nvgStrokeColor(vg, nvgRGB(0x00, 0x00, 0x00));
                     nvgStrokeWidth(vg, 3);
                     nvgStroke(vg);
                 })
                     .width(80)
                     .height(80))
                 .backgroundColor(t.bg)
                 .borderRadius(t.radiusSm)
                 .padding(t.gap),

             Label("Custom NanoVG drawing"),
         });
    };
}

// ============================================================================
// NVG Host
// ============================================================================

class NvgHost : public Host {
public:
    NvgHost(NVGcontext* vg, int fontId) : renderer_(vg, fontId) {
        setTextMeasurer(&renderer_);
        setRender(App("YUI Showcase", "NanoVG", CanvasDemo()));
    }

    void frame(int width, int height, float dt) {
        update(width, height, dt);
        renderer_.render(root());
    }

private:
    nvg::NvgRenderer renderer_;
};

// ============================================================================
// GLFW Callbacks
// ============================================================================

static NvgHost* g_host = nullptr;

static void cursorPosCallback(GLFWwindow*, double x, double y) {
    if (g_host)
        g_host->handleMouseMove(static_cast<float>(x), static_cast<float>(y));
}

// GLFW modifier bits -> yui KeyMod bitmask (shared by the key and mouse
// callbacks: presses carry mods too, for shift+click selection).
static uint16_t toKeyMod(int mods) {
    uint16_t keyMod = 0;
    if (mods & GLFW_MOD_SHIFT)
        keyMod |= KeyMod_Shift;
    if (mods & GLFW_MOD_CONTROL)
        keyMod |= KeyMod_Ctrl;
    if (mods & GLFW_MOD_ALT)
        keyMod |= KeyMod_Alt;
    if (mods & GLFW_MOD_SUPER)
        keyMod |= KeyMod_Super;
    if (mods & GLFW_MOD_CAPS_LOCK)
        keyMod |= KeyMod_CapsLock;
    if (mods & GLFW_MOD_NUM_LOCK)
        keyMod |= KeyMod_NumLock;
    return keyMod;
}

static void mouseButtonCallback(GLFWwindow*, int button, int action, int mods) {
    if (!g_host)
        return;

    double x, y;
    glfwGetCursorPos(glfwGetCurrentContext(), &x, &y);

    MouseButton mb = (button == GLFW_MOUSE_BUTTON_RIGHT)    ? MouseButton::Right
                     : (button == GLFW_MOUSE_BUTTON_MIDDLE) ? MouseButton::Middle
                                                            : MouseButton::Left;

    if (action == GLFW_PRESS) {
        g_host->handleMouseDown(static_cast<float>(x), static_cast<float>(y), mb, toKeyMod(mods));
    } else if (action == GLFW_RELEASE) {
        g_host->handleMouseUp(static_cast<float>(x), static_cast<float>(y), mb);
    }
}

static void scrollCallback(GLFWwindow*, double xoffset, double yoffset) {
    if (!g_host)
        return;

    double x, y;
    glfwGetCursorPos(glfwGetCurrentContext(), &x, &y);
    g_host->handleScroll(static_cast<float>(x), static_cast<float>(y), static_cast<float>(xoffset) * 20,
                         static_cast<float>(yoffset) * 20);
}

static void charCallback(GLFWwindow*, unsigned int codepoint) {
    if (!g_host)
        return;

    char buf[5] = {0};
    if (codepoint < 0x80) {
        buf[0] = static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        buf[0] = static_cast<char>(0xC0 | (codepoint >> 6));
        buf[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        buf[0] = static_cast<char>(0xE0 | (codepoint >> 12));
        buf[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        buf[0] = static_cast<char>(0xF0 | (codepoint >> 18));
        buf[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        buf[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        buf[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    g_host->handleTextInput(buf);
}

static void keyCallback(GLFWwindow* window, int key, int, int action, int mods) {
    if (!g_host)
        return;

    // Handle special keys for text input
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return;
        } else if (key == GLFW_KEY_BACKSPACE) {
            g_host->handleEditCommand(EditCommand::DeleteBackward);
        } else if (key == GLFW_KEY_ENTER) {
            g_host->handleEditCommand(EditCommand::InsertNewline);
        }
    }

    uint16_t keyMod = toKeyMod(mods);

    // Dispatch keyboard events
    if (action == GLFW_PRESS) {
        g_host->handleKeyDown(key, keyMod, false);
    } else if (action == GLFW_REPEAT) {
        g_host->handleKeyDown(key, keyMod, true);
    } else if (action == GLFW_RELEASE) {
        g_host->handleKeyUp(key, keyMod);
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1000, 700, "YUI Showcase", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW\n";
        glfwTerminate();
        return 1;
    }

    NVGcontext* vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!vg) {
        std::cerr << "Failed to create NanoVG context\n";
        glfwTerminate();
        return 1;
    }

    // Load font - try common paths for each OS
    int fontId = -1;
    const char* fontPaths[] = {// Windows
                               "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                               // Linux
                               "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                               "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                               // macOS
                               "/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/SFNS.ttf",
                               "/Library/Fonts/Arial.ttf", nullptr};
    for (const char** path = fontPaths; *path; path++) {
        fontId = nvgCreateFont(vg, "default", *path);
        if (fontId >= 0)
            break;
    }
    if (fontId < 0) {
        std::cerr << "Failed to load font\n";
        nvgDeleteGL3(vg);
        glfwTerminate();
        return 1;
    }

    {
        Store<AppState> store;
        storePtr = &store;

        NvgHost host(vg, fontId);
        g_host = &host;

        glfwSetCursorPosCallback(window, cursorPosCallback);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
        glfwSetScrollCallback(window, scrollCallback);
        glfwSetCharCallback(window, charCallback);
        glfwSetKeyCallback(window, keyCallback);

        while (!glfwWindowShouldClose(window)) {
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);

            int winWidth, winHeight;
            glfwGetWindowSize(window, &winWidth, &winHeight);
            float pxRatio = static_cast<float>(width) / static_cast<float>(winWidth);

            glViewport(0, 0, width, height);
            glClearColor(0x0a / 255.0f, 0x0a / 255.0f, 0x0a / 255.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

            nvgBeginFrame(vg, winWidth, winHeight, pxRatio);
            host.frame(winWidth, winHeight, 0.016f);
            nvgEndFrame(vg);

            glfwSwapBuffers(window);
            glfwPollEvents();
        }

        g_host = nullptr;
        storePtr = nullptr;
    }

    nvgDeleteGL3(vg);
    glfwTerminate();
    return 0;
}
