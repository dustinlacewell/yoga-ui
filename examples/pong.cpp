// Pong game example for yui (NanoVG backend)
// Demonstrates Canvas for game rendering + yui UI for score/controls
//
// Build: cmake --build build --target pong
// Run:   ./build/bin/pong
//
// Controls: W/S or Up/Down to move paddle, Space to start/reset

#include "yui/yui.hpp"
#include "yui/nvg/nvg.hpp"

#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <nanovg.h>
#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg_gl.h>

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace yui;

// Game constants
constexpr float PADDLE_WIDTH = 12.0f;
constexpr float PADDLE_HEIGHT = 80.0f;
constexpr float PADDLE_SPEED = 400.0f;
constexpr float BALL_SIZE = 12.0f;
constexpr float BALL_SPEED = 300.0f;
constexpr float AI_SPEED = 250.0f;

// Ball trail for motion effect
constexpr int TRAIL_LENGTH = 8;

// Game state
struct GameState {
    // Paddle positions (Y coordinate, X is fixed)
    float playerY = 0;
    float aiY = 0;

    // Ball position and velocity
    float ballX = 0;
    float ballY = 0;
    float ballVX = BALL_SPEED;
    float ballVY = BALL_SPEED * 0.5f;

    // Ball trail positions
    float trailX[TRAIL_LENGTH] = {};
    float trailY[TRAIL_LENGTH] = {};
    int trailIdx = 0;
    float trailTimer = 0;

    // Score
    int playerScore = 0;
    int aiScore = 0;

    // Game state
    bool running = false;
    bool gameOver = false;

    // Visual effects
    float playerHitFlash = 0;  // Flash timer when paddle hits ball
    float aiHitFlash = 0;
    float screenShake = 0;

    // Input
    bool upPressed = false;
    bool downPressed = false;

    // Canvas size (updated each frame)
    float canvasW = 600;
    float canvasH = 400;
};

Store<GameState>* store = nullptr;

// Colors - Neon arcade palette
constexpr uint32_t BG_COLOR = 0x0d0d1aFF;
constexpr uint32_t PANEL_COLOR = 0x1a1a2eFF;
constexpr uint32_t ACCENT_COLOR = 0x00ff88FF;   // Neon green
constexpr uint32_t PLAYER_COLOR = 0x00ff88FF;   // Neon green
constexpr uint32_t AI_COLOR = 0xff3366FF;       // Neon pink/red
constexpr uint32_t BALL_COLOR = 0xffffaaFF;     // Warm white/yellow
constexpr uint32_t TEXT_COLOR = 0xFFFFFFFF;
constexpr uint32_t MUTED_COLOR = 0x6a6a8aFF;

void resetBall(GameState& s) {
    s.ballX = s.canvasW / 2;
    s.ballY = s.canvasH / 2;
    s.ballVX = (s.ballVX > 0 ? -1 : 1) * BALL_SPEED;
    s.ballVY = ((rand() % 2) ? 1 : -1) * BALL_SPEED * (0.3f + (rand() % 100) / 200.0f);
}

void updateGame(GameState& s, float dt) {
    // Update visual effects (always, even when paused)
    if (s.playerHitFlash > 0) s.playerHitFlash -= dt * 4.0f;
    if (s.aiHitFlash > 0) s.aiHitFlash -= dt * 4.0f;
    if (s.screenShake > 0) s.screenShake -= dt * 8.0f;

    if (!s.running || s.gameOver)
        return;

    // Update ball trail
    s.trailTimer += dt;
    if (s.trailTimer > 0.016f) {  // ~60fps trail update
        s.trailTimer = 0;
        s.trailX[s.trailIdx] = s.ballX;
        s.trailY[s.trailIdx] = s.ballY;
        s.trailIdx = (s.trailIdx + 1) % TRAIL_LENGTH;
    }

    float paddleMargin = 20.0f;

    // Player movement
    if (s.upPressed)
        s.playerY -= PADDLE_SPEED * dt;
    if (s.downPressed)
        s.playerY += PADDLE_SPEED * dt;
    s.playerY = std::clamp(s.playerY, 0.0f, s.canvasH - PADDLE_HEIGHT);

    // Simple AI - follow the ball
    float aiCenter = s.aiY + PADDLE_HEIGHT / 2;
    float ballCenter = s.ballY + BALL_SIZE / 2;
    if (aiCenter < ballCenter - 20)
        s.aiY += AI_SPEED * dt;
    else if (aiCenter > ballCenter + 20)
        s.aiY -= AI_SPEED * dt;
    s.aiY = std::clamp(s.aiY, 0.0f, s.canvasH - PADDLE_HEIGHT);

    // Ball movement
    s.ballX += s.ballVX * dt;
    s.ballY += s.ballVY * dt;

    // Ball collision with top/bottom
    if (s.ballY <= 0) {
        s.ballY = 0;
        s.ballVY = -s.ballVY;
    }
    if (s.ballY >= s.canvasH - BALL_SIZE) {
        s.ballY = s.canvasH - BALL_SIZE;
        s.ballVY = -s.ballVY;
    }

    // Ball collision with player paddle
    float playerX = paddleMargin;
    if (s.ballX <= playerX + PADDLE_WIDTH && s.ballX + BALL_SIZE >= playerX && s.ballY + BALL_SIZE >= s.playerY &&
        s.ballY <= s.playerY + PADDLE_HEIGHT) {
        s.ballX = playerX + PADDLE_WIDTH;
        s.ballVX = std::abs(s.ballVX) * 1.05f;  // Speed up slightly
        // Add spin based on where ball hit paddle (scale with current speed)
        float hitPos = (s.ballY + BALL_SIZE / 2 - s.playerY) / PADDLE_HEIGHT;
        s.ballVY = std::abs(s.ballVX) * (hitPos - 0.5f) * 1.2f;
        s.playerHitFlash = 1.0f;  // Trigger flash effect
        s.screenShake = 0.3f;
    }

    // Ball collision with AI paddle
    float aiX = s.canvasW - paddleMargin - PADDLE_WIDTH;
    if (s.ballX + BALL_SIZE >= aiX && s.ballX <= aiX + PADDLE_WIDTH && s.ballY + BALL_SIZE >= s.aiY &&
        s.ballY <= s.aiY + PADDLE_HEIGHT) {
        s.ballX = aiX - BALL_SIZE;
        s.ballVX = -std::abs(s.ballVX) * 1.05f;
        // Add spin based on where ball hit paddle (scale with current speed)
        float hitPos = (s.ballY + BALL_SIZE / 2 - s.aiY) / PADDLE_HEIGHT;
        s.ballVY = std::abs(s.ballVX) * (hitPos - 0.5f) * 1.2f;
        s.aiHitFlash = 1.0f;  // Trigger flash effect
        s.screenShake = 0.3f;
    }

    // Scoring
    if (s.ballX < 0) {
        s.aiScore++;
        if (s.aiScore >= 5) {
            s.gameOver = true;
            s.running = false;
        } else {
            resetBall(s);
        }
    }
    if (s.ballX > s.canvasW) {
        s.playerScore++;
        if (s.playerScore >= 5) {
            s.gameOver = true;
            s.running = false;
        } else {
            resetBall(s);
        }
    }
}

// Game canvas component
VNode GameCanvas() {
    const auto& s = store->use();

    return Canvas([s](void* ctx, float w, float h) {
               auto* vg = static_cast<NVGcontext*>(ctx);

               // Update canvas size in state (hacky but works)
               if (w != s.canvasW || h != s.canvasH) {
                   store->set([w, h](GameState& gs) {
                       gs.canvasW = w;
                       gs.canvasH = h;
                       if (!gs.running && !gs.gameOver) {
                           gs.playerY = h / 2 - PADDLE_HEIGHT / 2;
                           gs.aiY = h / 2 - PADDLE_HEIGHT / 2;
                           gs.ballX = w / 2;
                           gs.ballY = h / 2;
                       }
                   });
               }

               // Screen shake offset
               float shakeX = s.screenShake > 0 ? (rand() % 5 - 2) * s.screenShake : 0;
               float shakeY = s.screenShake > 0 ? (rand() % 5 - 2) * s.screenShake : 0;

               nvgSave(vg);
               nvgTranslate(vg, shakeX, shakeY);

               float paddleMargin = 20.0f;

               // Background gradient
               nvgBeginPath(vg);
               nvgRect(vg, 0, 0, w, h);
               NVGpaint bgGrad = nvgLinearGradient(vg, 0, 0, 0, h,
                   nvgRGBA(0x0d, 0x0d, 0x1a, 0xFF),
                   nvgRGBA(0x1a, 0x0d, 0x1a, 0xFF));
               nvgFillPaint(vg, bgGrad);
               nvgFill(vg);

               // Subtle grid pattern
               nvgStrokeColor(vg, nvgRGBA(0x2a, 0x2a, 0x4a, 0x30));
               nvgStrokeWidth(vg, 1);
               for (float x = 0; x < w; x += 40) {
                   nvgBeginPath(vg);
                   nvgMoveTo(vg, x, 0);
                   nvgLineTo(vg, x, h);
                   nvgStroke(vg);
               }
               for (float y = 0; y < h; y += 40) {
                   nvgBeginPath(vg);
                   nvgMoveTo(vg, 0, y);
                   nvgLineTo(vg, w, y);
                   nvgStroke(vg);
               }

               // Center line (dashed, with glow)
               nvgStrokeColor(vg, nvgRGBA(0x4a, 0x4a, 0x7a, 0x60));
               nvgStrokeWidth(vg, 4);
               for (float y = 0; y < h; y += 24) {
                   nvgBeginPath(vg);
                   nvgMoveTo(vg, w / 2, y);
                   nvgLineTo(vg, w / 2, y + 12);
                   nvgStroke(vg);
               }
               nvgStrokeColor(vg, nvgRGBA(0x6a, 0x6a, 0x9a, 0xFF));
               nvgStrokeWidth(vg, 2);
               for (float y = 0; y < h; y += 24) {
                   nvgBeginPath(vg);
                   nvgMoveTo(vg, w / 2, y);
                   nvgLineTo(vg, w / 2, y + 12);
                   nvgStroke(vg);
               }

               // Ball trail
               for (int i = 0; i < TRAIL_LENGTH; i++) {
                   int idx = (s.trailIdx + i) % TRAIL_LENGTH;
                   float alpha = static_cast<float>(i) / TRAIL_LENGTH * 0.4f;
                   float size = BALL_SIZE * (0.3f + 0.5f * static_cast<float>(i) / TRAIL_LENGTH);
                   nvgBeginPath(vg);
                   nvgCircle(vg, s.trailX[idx] + BALL_SIZE / 2, s.trailY[idx] + BALL_SIZE / 2, size / 2);
                   nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0x88, static_cast<unsigned char>(alpha * 255)));
                   nvgFill(vg);
               }

               // Player paddle glow
               float playerGlow = std::max(0.0f, s.playerHitFlash);
               if (playerGlow > 0) {
                   nvgBeginPath(vg);
                   nvgRoundedRect(vg, paddleMargin - 8, s.playerY - 8, PADDLE_WIDTH + 16, PADDLE_HEIGHT + 16, 8);
                   NVGpaint glowPaint = nvgBoxGradient(vg, paddleMargin, s.playerY, PADDLE_WIDTH, PADDLE_HEIGHT, 4, 12,
                       nvgRGBA(0x00, 0xff, 0x88, static_cast<unsigned char>(playerGlow * 180)),
                       nvgRGBA(0x00, 0xff, 0x88, 0x00));
                   nvgFillPaint(vg, glowPaint);
                   nvgFill(vg);
               }

               // Player paddle
               nvgBeginPath(vg);
               nvgRoundedRect(vg, paddleMargin, s.playerY, PADDLE_WIDTH, PADDLE_HEIGHT, 3);
               NVGpaint playerGrad = nvgLinearGradient(vg, paddleMargin, 0, paddleMargin + PADDLE_WIDTH, 0,
                   nvgRGBA(0x00, 0xff, 0x99, 0xFF), nvgRGBA(0x00, 0xcc, 0x66, 0xFF));
               nvgFillPaint(vg, playerGrad);
               nvgFill(vg);
               // Paddle highlight
               nvgBeginPath(vg);
               nvgRoundedRect(vg, paddleMargin, s.playerY, 3, PADDLE_HEIGHT, 2);
               nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x40));
               nvgFill(vg);

               // AI paddle glow
               float aiGlow = std::max(0.0f, s.aiHitFlash);
               if (aiGlow > 0) {
                   nvgBeginPath(vg);
                   nvgRoundedRect(vg, w - paddleMargin - PADDLE_WIDTH - 8, s.aiY - 8, PADDLE_WIDTH + 16, PADDLE_HEIGHT + 16, 8);
                   NVGpaint glowPaint = nvgBoxGradient(vg, w - paddleMargin - PADDLE_WIDTH, s.aiY, PADDLE_WIDTH, PADDLE_HEIGHT, 4, 12,
                       nvgRGBA(0xff, 0x33, 0x66, static_cast<unsigned char>(aiGlow * 180)),
                       nvgRGBA(0xff, 0x33, 0x66, 0x00));
                   nvgFillPaint(vg, glowPaint);
                   nvgFill(vg);
               }

               // AI paddle
               nvgBeginPath(vg);
               nvgRoundedRect(vg, w - paddleMargin - PADDLE_WIDTH, s.aiY, PADDLE_WIDTH, PADDLE_HEIGHT, 3);
               NVGpaint aiGrad = nvgLinearGradient(vg, w - paddleMargin - PADDLE_WIDTH, 0, w - paddleMargin, 0,
                   nvgRGBA(0xcc, 0x22, 0x55, 0xFF), nvgRGBA(0xff, 0x44, 0x77, 0xFF));
               nvgFillPaint(vg, aiGrad);
               nvgFill(vg);
               // Paddle highlight
               nvgBeginPath(vg);
               nvgRoundedRect(vg, w - paddleMargin - 3, s.aiY, 3, PADDLE_HEIGHT, 2);
               nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x40));
               nvgFill(vg);

               // Ball glow
               nvgBeginPath(vg);
               nvgCircle(vg, s.ballX + BALL_SIZE / 2, s.ballY + BALL_SIZE / 2, BALL_SIZE * 1.5f);
               NVGpaint ballGlow = nvgRadialGradient(vg, s.ballX + BALL_SIZE / 2, s.ballY + BALL_SIZE / 2,
                   BALL_SIZE / 2, BALL_SIZE * 2,
                   nvgRGBA(0xff, 0xff, 0x88, 0x60), nvgRGBA(0xff, 0xff, 0x88, 0x00));
               nvgFillPaint(vg, ballGlow);
               nvgFill(vg);

               // Ball
               nvgBeginPath(vg);
               nvgCircle(vg, s.ballX + BALL_SIZE / 2, s.ballY + BALL_SIZE / 2, BALL_SIZE / 2);
               NVGpaint ballPaint = nvgRadialGradient(vg, s.ballX + BALL_SIZE / 2 - 2, s.ballY + BALL_SIZE / 2 - 2,
                   1, BALL_SIZE,
                   nvgRGBA(0xff, 0xff, 0xff, 0xFF), nvgRGBA(0xff, 0xee, 0x88, 0xFF));
               nvgFillPaint(vg, ballPaint);
               nvgFill(vg);

               // Game over / start text
               if (!s.running) {
                   // Darken overlay
                   nvgBeginPath(vg);
                   nvgRect(vg, 0, 0, w, h);
                   nvgFillColor(vg, nvgRGBA(0x00, 0x00, 0x00, 0x80));
                   nvgFill(vg);

                   nvgFontSize(vg, 32);
                   nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

                   if (s.gameOver) {
                       const char* msg = s.playerScore >= 5 ? "YOU WIN!" : "AI WINS!";
                       uint32_t msgColor = s.playerScore >= 5 ? PLAYER_COLOR : AI_COLOR;
                       // Text glow
                       nvgFontBlur(vg, 4);
                       nvgFillColor(vg, nvgRGBA((msgColor >> 24) & 0xFF, (msgColor >> 16) & 0xFF, (msgColor >> 8) & 0xFF, 0x80));
                       nvgText(vg, w / 2, h / 2 - 20, msg, nullptr);
                       // Main text
                       nvgFontBlur(vg, 0);
                       nvgFillColor(vg, nvgRGBA((msgColor >> 24) & 0xFF, (msgColor >> 16) & 0xFF, (msgColor >> 8) & 0xFF, 0xFF));
                       nvgText(vg, w / 2, h / 2 - 20, msg, nullptr);

                       nvgFontSize(vg, 16);
                       nvgFillColor(vg, nvgRGBA(0xaa, 0xaa, 0xcc, 0xFF));
                       nvgText(vg, w / 2, h / 2 + 25, "Press SPACE to play again", nullptr);
                   } else {
                       // Pulsing "Press SPACE" text
                       nvgFillColor(vg, nvgRGBA(0xcc, 0xcc, 0xff, 0xFF));
                       nvgText(vg, w / 2, h / 2, "Press SPACE to start", nullptr);
                   }
               }

               nvgRestore(vg);
           })
        .flexGrow(1);
}

// Score display component
VNode ScoreBoard() {
    const auto& s = store->use();

    // Calculate ball speed in pixels per second
    float speed = std::sqrt(s.ballVX * s.ballVX + s.ballVY * s.ballVY);
    int speedInt = static_cast<int>(speed);

    return Column(
                      Row(
                              // Player score
                              Column(
                                         Text("PLAYER").fontSize(11).color(MUTED_COLOR),
                                         Text(std::to_string(s.playerScore)).fontSize(52).color(PLAYER_COLOR)
                                     )
                                  .alignItems(AlignItems::Center)
                                  .flexGrow(1),

                              // Ball speed in center
                              Column(
                                         Text("SPEED").fontSize(9).color(MUTED_COLOR),
                                         Text(std::to_string(speedInt)).fontSize(20).color(BALL_COLOR),
                                         Text("px/s").fontSize(9).color(MUTED_COLOR)
                                     )
                                  .alignItems(AlignItems::Center)
                                  .paddingLeft(16)
                                  .paddingRight(16)
                                  .paddingTop(12)
                                  .paddingBottom(12),

                              // AI score
                              Column(
                                         Text("CPU").fontSize(11).color(MUTED_COLOR),
                                         Text(std::to_string(s.aiScore)).fontSize(52).color(AI_COLOR)
                                     )
                                  .alignItems(AlignItems::Center)
                                  .flexGrow(1)
                          )
                          .alignItems(AlignItems::Center)
                  )
        .paddingTop(16)
        .paddingBottom(16)
        .paddingLeft(20)
        .paddingRight(20)
        .backgroundColor(PANEL_COLOR);
}

// Controls help
VNode ControlsHelp() {
    return Row(
                   Text("W/S").fontSize(11).color(PLAYER_COLOR),
                   Text(" or ").fontSize(11).color(MUTED_COLOR),
                   Text("\u2191/\u2193").fontSize(11).color(PLAYER_COLOR),
                   Text(" to move").fontSize(11).color(MUTED_COLOR),
                   Box().flexGrow(1),
                   Text("SPACE").fontSize(11).color(BALL_COLOR),
                   Text(" to start/reset").fontSize(11).color(MUTED_COLOR)
               )
        .paddingTop(10)
        .paddingBottom(10)
        .paddingLeft(16)
        .paddingRight(16)
        .backgroundColor(PANEL_COLOR)
        .alignItems(AlignItems::Center);
}

VNode buildUI() {
    return Column(
                      // Score at top
                      ScoreBoard(),

                      // Game canvas in middle
                      GameCanvas(),

                      // Controls at bottom
                      ControlsHelp()
                  )
        .flexGrow(1)
        .backgroundColor(BG_COLOR);
}

// Host
class PongHost : public Host {
public:
    PongHost(NVGcontext* vg, int fontId) : renderer_(vg, fontId) {
        setTextMeasurer(&renderer_);
        setRender(buildUI);
    }

    void frame(int w, int h, float dt) {
        // Update game physics
        store->set([dt](GameState& s) { updateGame(s, dt); });

        update(w, h, dt);
        renderer_.render(root());
    }

private:
    nvg::NvgRenderer renderer_;
};

// Globals
static PongHost* g_host = nullptr;

static void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
        store->set([](GameState& s) {
            if (s.gameOver) {
                s.playerScore = 0;
                s.aiScore = 0;
                s.gameOver = false;
            }
            if (!s.running) {
                s.running = true;
                resetBall(s);
            }
        });
    }

    if (key == GLFW_KEY_W || key == GLFW_KEY_UP) {
        store->set([action](GameState& s) { s.upPressed = (action != GLFW_RELEASE); });
    }
    if (key == GLFW_KEY_S || key == GLFW_KEY_DOWN) {
        store->set([action](GameState& s) { s.downPressed = (action != GLFW_RELEASE); });
    }
}

int main() {
    srand(static_cast<unsigned>(time(nullptr)));

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

    GLFWwindow* window = glfwCreateWindow(700, 500, "Pong - yui", nullptr, nullptr);
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

    // Load font
    int fontId = -1;
    const char* fontPaths[] = {"C:/Windows/Fonts/segoeui.ttf",
                               "C:/Windows/Fonts/arial.ttf",
                               "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                               "/System/Library/Fonts/Helvetica.ttc",
                               nullptr};
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
        Store<GameState> gameStore;
        store = &gameStore;

        PongHost host(vg, fontId);
        g_host = &host;

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
            glClearColor(0.04f, 0.04f, 0.08f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

            nvgBeginFrame(vg, winW, winH, pxRatio);
            host.frame(winW, winH, dt);
            nvgEndFrame(vg);

            glfwSwapBuffers(window);
            glfwPollEvents();
        }

        g_host = nullptr;
        store = nullptr;
    }

    nvgDeleteGL3(vg);
    glfwTerminate();
    return 0;
}
