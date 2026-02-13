// Quad-tree benchmark for yui (NanoVG backend)
// Click any square to subdivide it into 4 smaller squares.
// The tree grows exponentially, stress-testing reconciliation and layout.
//
// Build: cmake --build build --target benchmark
// Run:   ./build/bin/benchmark

#include "yui/yui.hpp"
#include "yui/nvg/nvg.hpp"

#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <nanovg.h>
#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg_gl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>

using namespace yui;

// Recursive quad-tree structure
struct Quad {
    uint32_t color;
    bool subdivided = false;
    std::unique_ptr<Quad> children[4];  // TL, TR, BL, BR

    explicit Quad(uint32_t c) : color(c) {}

    void subdivide() {
        if (subdivided)
            return;
        subdivided = true;
        for (int i = 0; i < 4; i++) {
            // Vary color for each child - create visual distinction
            int r = static_cast<int>((color >> 24) & 0xFF);
            int g = static_cast<int>((color >> 16) & 0xFF);
            int b = static_cast<int>((color >> 8) & 0xFF);

            // Each quadrant gets a different color shift
            switch (i) {
            case 0:
                r = std::min(255, r + 20);
                break;  // TL: more red
            case 1:
                g = std::min(255, g + 20);
                break;  // TR: more green
            case 2:
                b = std::min(255, b + 20);
                break;  // BL: more blue
            case 3:
                r = std::max(0, r - 15);
                g = std::max(0, g - 15);
                break;  // BR: darker
            }

            children[i] = std::make_unique<Quad>(
                static_cast<uint32_t>((r << 24) | (g << 16) | (b << 8) | 0xFF));
        }
    }

    int countNodes() const {
        if (!subdivided)
            return 1;
        int sum = 1;
        for (const auto& c : children) {
            if (c)
                sum += c->countNodes();
        }
        return sum;
    }

    int maxDepth() const {
        if (!subdivided)
            return 1;
        int max = 0;
        for (const auto& c : children) {
            if (c)
                max = std::max(max, c->maxDepth());
        }
        return 1 + max;
    }
};

// Quad tree state (changes rarely - only on clicks)
struct QuadState {
    Quad root{0x3366CCFF};
};

// Stats state (changes every frame)
struct StatsState {
    float fps = 0;
    float frameTime = 0;
    float updateTime = 0;
    float renderTime = 0;
    int nodeCount = 0;
    int depth = 0;
};

Store<QuadState>* quadStore = nullptr;
Store<StatsState>* statsStore = nullptr;

// Lighten a color by adding to RGB channels
inline uint32_t lighten(uint32_t color, int amount) {
    int r = std::min(255, static_cast<int>((color >> 24) & 0xFF) + amount);
    int g = std::min(255, static_cast<int>((color >> 16) & 0xFF) + amount);
    int b = std::min(255, static_cast<int>((color >> 8) & 0xFF) + amount);
    return static_cast<uint32_t>((r << 24) | (g << 16) | (b << 8) | 0xFF);
}

// Recursive component - renders a quad as either a leaf or 2x2 grid
// No keys needed - structure is deterministic, position-based diffing works fine
Component QuadView(Quad& quad) {
    return [&quad](ComponentContext&) -> VNode {
        quadStore->use();  // Force re-render when store changes
        if (!quad.subdivided) {
            // Leaf node - clickable square
            uint32_t hoverColor = lighten(quad.color, 30);

            return Box()
                .flexGrow(1)
                .backgroundColor(quad.color)
                .hoverStyle(BoxStyle{.backgroundColor = hoverColor})
                .onClick([&quad] {
                    quadStore->set([&](QuadState&) { quad.subdivide(); });
                });
        }

        // Subdivided - 2x2 grid of children
        return Column({
                          Row({QuadView(*quad.children[0]), QuadView(*quad.children[1])}).flexGrow(1).gap(1),
                          Row({QuadView(*quad.children[2]), QuadView(*quad.children[3])}).flexGrow(1).gap(1),
                      })
            .flexGrow(1)
            .gap(1);
    };
}

// Colors for stats overlay
constexpr uint32_t STAT_TEXT_COLOR = 0xFFFFFFFF;
constexpr uint32_t STAT_MUTED_COLOR = 0xAAAAAAFF;
constexpr uint32_t STAT_ACCENT_COLOR = 0x00FF88FF;

// Helper to create a stat line
Component StatLine(const std::string& label, const std::string& value, uint32_t valueColor = STAT_TEXT_COLOR) {
    return [=](ComponentContext&) -> VNode {
        return Row({
                       Text(label).fontSize(12).color(STAT_MUTED_COLOR),
                       Text(value).fontSize(12).color(valueColor),
                   })
            .gap(4);
    };
}

// Stats overlay - subscribes only to stats store
Component StatsOverlay() {
    return [](ComponentContext&) -> VNode {
        const auto& s = statsStore->use();

        // Color FPS based on performance
        uint32_t fpsColor = STAT_ACCENT_COLOR;
        if (s.fps < 30)
            fpsColor = 0xFF4444FF;  // Red
        else if (s.fps < 55)
            fpsColor = 0xFFAA44FF;  // Orange

        // Format timing values
        char frameBuf[32], updateBuf[32], renderBuf[32];
        snprintf(frameBuf, sizeof(frameBuf), "%.2f ms", s.frameTime);
        snprintf(updateBuf, sizeof(updateBuf), "%.2f ms", s.updateTime);
        snprintf(renderBuf, sizeof(renderBuf), "%.2f ms", s.renderTime);

        return Column({
                          Text("YUI Benchmark").fontSize(16).color(STAT_TEXT_COLOR),
                          Box().height(8),
                          StatLine("Nodes: ", std::to_string(s.nodeCount)),
                          StatLine("Depth: ", std::to_string(s.depth)),
                          Box().height(4),
                          StatLine("FPS: ", std::to_string(static_cast<int>(s.fps)), fpsColor),
                          StatLine("Frame: ", frameBuf),
                          StatLine("Update: ", updateBuf),
                          StatLine("Render: ", renderBuf),
                          Box().height(8),
                          Text("Click squares to subdivide").fontSize(11).color(STAT_MUTED_COLOR),
                          Text("Press R to reset").fontSize(11).color(STAT_MUTED_COLOR),
                      })
            .positionType(PositionType::Absolute)
            .positionTop(12)
            .positionLeft(12)
            .padding(12)
            .backgroundColor(0x000000DD)
            .borderRadius(6);
    };
}

// QuadTree view - subscribes only to quad store
Component QuadTreeView() {
    return [](ComponentContext&) -> VNode {
        auto& q = const_cast<QuadState&>(quadStore->use());
        return Box({QuadView(q.root)}).flexGrow(1);
    };
}

VNode buildUI() {
    return Box({
                   QuadTreeView(),
                   StatsOverlay(),
               })
        .flexGrow(1)
        .padding(2)
        .backgroundColor(0x111111FF);
}

// Host with timing instrumentation
class BenchHost : public Host {
public:
    BenchHost(NVGcontext* vg, int fontId) : renderer_(vg, fontId) {
        renderer_.registerMeasureFunc();
        setRender(buildUI);
    }

    void frame(int w, int h, float dt) {
        using Clock = std::chrono::high_resolution_clock;

        auto t0 = Clock::now();
        update(w, h, dt);  // reconcile + layout
        auto t1 = Clock::now();
        renderer_.render(root());
        auto t2 = Clock::now();

        float updateMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
        float renderMs = std::chrono::duration<float, std::milli>(t2 - t1).count();
        float frameMs = updateMs + renderMs;
        float fps = dt > 0 ? 1.0f / dt : 0;

        // Update stats (only rebuilds stats overlay, not quad tree)
        const auto& q = quadStore->peek();
        int nodeCount = q.root.countNodes();
        int depth = q.root.maxDepth();

        statsStore->set([=](StatsState& s) {
            s.fps = s.fps * 0.9f + fps * 0.1f;
            s.frameTime = s.frameTime * 0.9f + frameMs * 0.1f;
            s.updateTime = s.updateTime * 0.9f + updateMs * 0.1f;
            s.renderTime = s.renderTime * 0.9f + renderMs * 0.1f;
            s.nodeCount = nodeCount;
            s.depth = depth;
        });
    }

private:
    nvg::NvgRenderer renderer_;
};

// Globals for GLFW callbacks
static BenchHost* g_host = nullptr;

static void cursorPosCallback(GLFWwindow*, double x, double y) {
    if (g_host)
        g_host->handleMouseMove(static_cast<float>(x), static_cast<float>(y));
}

static void mouseButtonCallback(GLFWwindow*, int button, int action, int) {
    if (!g_host)
        return;

    double x, y;
    glfwGetCursorPos(glfwGetCurrentContext(), &x, &y);

    MouseButton mb = (button == GLFW_MOUSE_BUTTON_RIGHT) ? MouseButton::Right : MouseButton::Left;

    if (action == GLFW_PRESS) {
        g_host->handleMouseDown(static_cast<float>(x), static_cast<float>(y), mb);
    } else if (action == GLFW_RELEASE) {
        g_host->handleMouseUp(static_cast<float>(x), static_cast<float>(y), mb);
    }
}

static void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    // Reset on R key
    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        quadStore->set([](QuadState& s) { s.root = Quad(0x3366CCFF); });
    }
}

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

    GLFWwindow* window = glfwCreateWindow(800, 800, "YUI Benchmark", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // VSync on

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

    // Load font
    int fontId = -1;
    const char* fontPaths[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        nullptr,
    };
    for (const char** p = fontPaths; *p; p++) {
        fontId = nvgCreateFont(vg, "default", *p);
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
        Store<QuadState> quadStateStore;
        Store<StatsState> statsStateStore;
        quadStore = &quadStateStore;
        statsStore = &statsStateStore;

        BenchHost host(vg, fontId);
        g_host = &host;

        glfwSetCursorPosCallback(window, cursorPosCallback);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
        glfwSetKeyCallback(window, keyCallback);

        double lastTime = glfwGetTime();

        while (!glfwWindowShouldClose(window)) {
            double currentTime = glfwGetTime();
            float dt = static_cast<float>(currentTime - lastTime);
            lastTime = currentTime;

            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            int winW, winH;
            glfwGetWindowSize(window, &winW, &winH);
            float pxRatio = static_cast<float>(width) / static_cast<float>(winW);

            glViewport(0, 0, width, height);
            glClearColor(0.067f, 0.067f, 0.067f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

            nvgBeginFrame(vg, winW, winH, pxRatio);
            host.frame(winW, winH, dt);
            nvgEndFrame(vg);

            glfwSwapBuffers(window);
            glfwPollEvents();
        }

        g_host = nullptr;
        quadStore = nullptr;
        statsStore = nullptr;
    }

    nvgDeleteGL3(vg);
    glfwTerminate();
    return 0;
}
