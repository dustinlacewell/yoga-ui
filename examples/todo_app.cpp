// Simple Todo App example for yui (NanoVG backend)
//
// Build: cmake --build build --target todo_app
// Run:   ./build/bin/todo_app

#include "yui/yui.hpp"
#include "yui/nvg/nvg.hpp"

#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <nanovg.h>
#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg_gl.h>

#include <algorithm>
#include <iostream>
#include <optional>
#include <vector>

using namespace yui;

// App state
struct TodoItem {
    int id;
    std::string text;
    bool completed;
};

struct AppState {
    std::vector<TodoItem> todos;
    int nextId = 1;
};

Store<AppState>* store = nullptr;

// Colors
constexpr uint32_t BG_COLOR = 0x1a1a2eFF;
constexpr uint32_t CARD_COLOR = 0x16213eFF;
constexpr uint32_t ACCENT_COLOR = 0x0f3460FF;
constexpr uint32_t TEXT_COLOR = 0xFFFFFFFF;
constexpr uint32_t MUTED_COLOR = 0x888888FF;
constexpr uint32_t GREEN_COLOR = 0x4ade80FF;
constexpr uint32_t RED_COLOR = 0xf87171FF;

// Checkbox component using Canvas
VNode Checkbox(bool checked, std::function<void()> onToggle) {
    return Box(Canvas([checked](void* ctx, float w, float h) {
                    auto* vg = static_cast<NVGcontext*>(ctx);
                    float size = std::min(w, h);
                    float x = (w - size) / 2;
                    float y = (h - size) / 2;
                    float r = 3.0f;

                    // Box outline
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, x + 1, y + 1, size - 2, size - 2, r);
                    if (checked) {
                        nvgFillColor(vg, nvgRGBA(0x4a, 0xde, 0x80, 0xFF));  // Green
                        nvgFill(vg);
                    } else {
                        nvgStrokeColor(vg, nvgRGBA(0x88, 0x88, 0x88, 0xFF));  // Gray
                        nvgStrokeWidth(vg, 2.0f);
                        nvgStroke(vg);
                    }

                    // Checkmark
                    if (checked) {
                        nvgBeginPath(vg);
                        nvgMoveTo(vg, x + size * 0.25f, y + size * 0.5f);
                        nvgLineTo(vg, x + size * 0.45f, y + size * 0.7f);
                        nvgLineTo(vg, x + size * 0.75f, y + size * 0.3f);
                        nvgStrokeColor(vg, nvgRGBA(0xFF, 0xFF, 0xFF, 0xFF));
                        nvgStrokeWidth(vg, 2.5f);
                        nvgLineCap(vg, NVG_ROUND);
                        nvgLineJoin(vg, NVG_ROUND);
                        nvgStroke(vg);
                    }
                })
                    .width(20)
                    .height(20))
        .padding(8)
        .onClick(std::move(onToggle));
}

// Components
// Input component using useState for component-local draft text
auto TodoInput() -> Component {
    return [](ComponentContext& ctx) -> VNode {
        // useState for component-local state (not shared with other components)
        auto [draft, setDraft] = ctx.useState<std::string>("");

        auto addTodo = [draft, setDraft]() {
            if (!draft.empty()) {
                store->set([text = draft](AppState& s) { s.todos.push_back({s.nextId++, text, false}); });
                setDraft("");
            }
        };

        return Row(
            Input()
                .value(draft)
                .onChange(setDraft)
                .placeholder("Add a new todo...")
                .fontSize(16)
                .color(TEXT_COLOR)
                .backgroundColor(CARD_COLOR)
                .borderRadius(4)
                .padding(12)
                .flexGrow(1)
                .onSubmit(addTodo),

            Box(Text("Add").fontSize(16).color(TEXT_COLOR))
                .backgroundColor(ACCENT_COLOR)
                .borderRadius(4)
                .padding(12)
                .marginLeft(8)
                .onClick(addTodo)
        );
    };
}

VNode TodoItemView(const TodoItem& item) {
    return Row(
                   // Checkbox
                   Checkbox(item.completed, [id = item.id]() {
                       store->set([id](AppState& s) {
                           for (auto& t : s.todos) {
                               if (t.id == id) {
                                   t.completed = !t.completed;
                                   break;
                               }
                           }
                       });
                   }),

                   // Text
                   Box(Text(item.text).fontSize(16).color(item.completed ? MUTED_COLOR : TEXT_COLOR))
                       .flexGrow(1)
                       .justifyContent(JustifyContent::Center),

                   // Delete button (circle with X)
                   Box(Canvas([](void* ctx, float w, float h) {
                            auto* vg = static_cast<NVGcontext*>(ctx);
                            float size = std::min(w, h);
                            float cx = w / 2;
                            float cy = h / 2;
                            float r = size / 2 - 2;

                            // Circle
                            nvgBeginPath(vg);
                            nvgCircle(vg, cx, cy, r);
                            nvgStrokeColor(vg, nvgRGBA(0xf8, 0x71, 0x71, 0xFF));  // Red
                            nvgStrokeWidth(vg, 1.5f);
                            nvgStroke(vg);

                            // X lines
                            float offset = r * 0.5f;
                            nvgBeginPath(vg);
                            nvgMoveTo(vg, cx - offset, cy - offset);
                            nvgLineTo(vg, cx + offset, cy + offset);
                            nvgMoveTo(vg, cx + offset, cy - offset);
                            nvgLineTo(vg, cx - offset, cy + offset);
                            nvgStrokeColor(vg, nvgRGBA(0xf8, 0x71, 0x71, 0xFF));
                            nvgStrokeWidth(vg, 1.5f);
                            nvgLineCap(vg, NVG_ROUND);
                            nvgStroke(vg);
                        })
                            .width(20)
                            .height(20))
                       .padding(8)
                       .onClick([id = item.id]() {
                           store->set([id](AppState& s) {
                               s.todos.erase(std::remove_if(s.todos.begin(), s.todos.end(),
                                                            [id](const TodoItem& t) { return t.id == id; }),
                                             s.todos.end());
                           });
                       })
               )
        .backgroundColor(CARD_COLOR)
        .borderRadius(4)
        .marginBottom(8)
        .setKey(std::to_string(item.id));
}

Component TodoApp() {
    return [](ComponentContext&) -> VNode {
        const auto& state = store->use();

        std::vector<Child> todoItems;
        for (const auto& item : state.todos) {
            todoItems.push_back(TodoItemView(item));
        }

        return Column(
                          // Title
                          Text("Todo App").fontSize(28).color(TEXT_COLOR).marginBottom(20),

                          // Input row (component with local state for draft text)
                          TodoInput(),

                          Gap(20),

                          // Todo list
                          Scroll(Column(std::move(todoItems)).flexGrow(1)).flexGrow(1),

                          // Footer
                          Text(std::to_string(state.todos.size()) + " items")
                              .fontSize(12)
                              .color(MUTED_COLOR)
                              .marginTop(12)
                      )
            .padding(24)
            .flexGrow(1)
            .backgroundColor(BG_COLOR);
    };
}

// GLFW system clipboard: the IClipboard seam over glfwGet/SetClipboardString.
// Lives in the example, not yui/nvg — the nvg module is windowing-agnostic and
// the clipboard belongs to the windowing layer (it needs a GLFWwindow*).
class GlfwClipboard : public IClipboard {
public:
    explicit GlfwClipboard(GLFWwindow* window) : window_(window) {}

    std::string getText() override {
        // NULL when the clipboard is empty or holds non-text content.
        const char* text = glfwGetClipboardString(window_);
        return text ? std::string(text) : std::string();
    }

    void setText(const std::string& text) override { glfwSetClipboardString(window_, text.c_str()); }

private:
    GLFWwindow* window_;
};

// Host
class TodoHost : public Host {
public:
    TodoHost(NVGcontext* vg, int fontId, GLFWwindow* window) : renderer_(vg, fontId), clipboard_(window) {
        setTextMeasurer(&renderer_);
        setClipboard(&clipboard_);
        setRender(TodoApp());
    }

    void frame(int w, int h, float dt) {
        update(w, h, dt);
        renderer_.render(root());
    }

private:
    nvg::NvgRenderer renderer_;
    GlfwClipboard clipboard_;
};

// Globals for callbacks
static TodoHost* g_host = nullptr;

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
    if (action == GLFW_PRESS)
        g_host->handleMouseDown(static_cast<float>(x), static_cast<float>(y), mb, toKeyMod(mods));
    else if (action == GLFW_RELEASE)
        g_host->handleMouseUp(static_cast<float>(x), static_cast<float>(y), mb);
}

static void scrollCallback(GLFWwindow*, double xoff, double yoff) {
    if (!g_host)
        return;
    double x, y;
    glfwGetCursorPos(glfwGetCurrentContext(), &x, &y);
    g_host->handleScroll(static_cast<float>(x), static_cast<float>(y), static_cast<float>(xoff) * 20,
                         static_cast<float>(yoff) * 20);
}

static void charCallback(GLFWwindow*, unsigned int codepoint) {
    if (!g_host)
        return;
    char buf[5] = {0};
    if (codepoint < 0x80)
        buf[0] = static_cast<char>(codepoint);
    else if (codepoint < 0x800) {
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

// The editing command a key maps to, if any — routed to the focused Input only
// after handleKeyDown reports the key unconsumed (the Tab precedent: app
// handlers see the raw key first). Enter maps to InsertNewline below (core
// decides: multiline inserts '\n', single-line fires onSubmit).
static std::optional<EditCommand> editCommandFor(int key) {
    switch (key) {
    case GLFW_KEY_LEFT:
        return EditCommand::MoveLeft;
    case GLFW_KEY_RIGHT:
        return EditCommand::MoveRight;
    case GLFW_KEY_HOME:
        return EditCommand::MoveLineStart;
    case GLFW_KEY_END:
        return EditCommand::MoveLineEnd;
    case GLFW_KEY_BACKSPACE:
        return EditCommand::DeleteBackward;
    case GLFW_KEY_DELETE:
        return EditCommand::DeleteForward;
    default:
        return std::nullopt;
    }
}

static void keyCallback(GLFWwindow* window, int key, int, int action, int mods) {
    if (!g_host)
        return;
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return;
        } else if (key == GLFW_KEY_ENTER) {
            g_host->handleEditCommand(EditCommand::InsertNewline);
        }
    }
    uint16_t keyMod = toKeyMod(mods);
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        // Tab traversal and the editing keys live in the platform shim (core
        // stays keycode-agnostic; GLFW's Tab is 258), and only when no app
        // handler consumed the key.
        bool consumed = g_host->handleKeyDown(key, keyMod, action == GLFW_REPEAT);
        if (!consumed) {
            if (key == GLFW_KEY_TAB) {
                if (keyMod & KeyMod_Shift)
                    g_host->focusPrev();
                else
                    g_host->focusNext();
            } else if (key == GLFW_KEY_A && (keyMod & KeyMod_Ctrl)) {
                g_host->handleEditCommand(EditCommand::SelectAll);
            } else if (key == GLFW_KEY_C && (keyMod & KeyMod_Ctrl)) {
                g_host->handleEditCommand(EditCommand::Copy);
            } else if (key == GLFW_KEY_X && (keyMod & KeyMod_Ctrl)) {
                g_host->handleEditCommand(EditCommand::Cut);
            } else if (key == GLFW_KEY_V && (keyMod & KeyMod_Ctrl)) {
                g_host->handleEditCommand(EditCommand::Paste);
            } else if (auto cmd = editCommandFor(key)) {
                // Shift extends the selection: moves shift only the caret,
                // leaving the anchor (deletes ignore the flag).
                g_host->handleEditCommand(*cmd, (keyMod & KeyMod_Shift) != 0);
            }
        }
    } else if (action == GLFW_RELEASE) {
        g_host->handleKeyUp(key, keyMod);
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

    GLFWwindow* window = glfwCreateWindow(400, 600, "Todo App - yui", nullptr, nullptr);
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
        Store<AppState> appStore;
        store = &appStore;

        // Add some sample todos
        store->set([](AppState& s) {
            s.todos.push_back({s.nextId++, "Learn yui framework", false});
            s.todos.push_back({s.nextId++, "Build something cool", false});
            s.todos.push_back({s.nextId++, "Have fun!", true});
        });

        TodoHost host(vg, fontId, window);
        g_host = &host;

        glfwSetCursorPosCallback(window, cursorPosCallback);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
        glfwSetScrollCallback(window, scrollCallback);
        glfwSetCharCallback(window, charCallback);
        glfwSetKeyCallback(window, keyCallback);

        while (!glfwWindowShouldClose(window)) {
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            int winW, winH;
            glfwGetWindowSize(window, &winW, &winH);
            float pxRatio = static_cast<float>(width) / static_cast<float>(winW);

            glViewport(0, 0, width, height);
            glClearColor(0.1f, 0.1f, 0.18f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

            nvgBeginFrame(vg, winW, winH, pxRatio);
            host.frame(winW, winH, 1.0f / 60.0f);
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
