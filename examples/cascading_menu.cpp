// Cascading Menu example for yui (NanoVG backend)
//
// Demonstrates the "root-level submenu rendering" pattern for building
// traditional cascading menus with a declarative UI framework:
//
//   - Menu bar with click-to-open dropdowns
//   - Hover-to-open cascading submenus
//   - Root-level rendering avoids Scroll clipping (the "portal" pattern)
//   - Hover-in drives submenu switching (hover-out is ignored, allowing
//     the mouse to travel into the submenu without it closing)
//
// The File menu has enough items to scroll, proving that submenus
// render correctly outside the scroll boundary.
//
// Build: cmake --build build --target cascading_menu
// Run:   ./build/bin/cascading_menu

#include "yui/yui.hpp"
#include "yui/nvg/nvg.hpp"
#include "yui/layout/Placement.hpp"

#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <nanovg.h>
#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg_gl.h>

#include <iostream>
#include <string>
#include <vector>

using namespace yui;

// ─── Menu definition ────────────────────────────────────────────────────────

struct MenuDef {
    std::string label;
    std::string shortcut;
    std::vector<MenuDef> children;
    bool separator = false;
    float maxHeight = 0;  // If > 0, clamp panel height and scroll
};

static MenuDef Sep() { return {"", "", {}, true}; }
static bool hasKids(const MenuDef& d) { return !d.children.empty(); }

static const std::vector<MenuDef> g_menuBar = {
    {"File", "", {
        {"New", "Ctrl+N", {}},
        {"New from Template", "", {
            {"Empty Patch", "", {}},
            {"Basic Synth", "", {}},
            {"Drum Machine", "", {}},
            {"Ambient", "", {}},
        }},
        {"Open...", "Ctrl+O", {}},
        {"Open Recent", "", {
            {"project_v2.vcv", "", {}},
            {"ambient_patch.vcv", "", {}},
            {"bass_drone.vcv", "", {}},
            {"generative_01.vcv", "", {}},
            {"friday_live_set.vcv", "", {}},
        }},
        Sep(),
        {"Save", "Ctrl+S", {}},
        {"Save As...", "Ctrl+Shift+S", {}},
        Sep(),
        {"Import", "", {
            {"VCV Selection", "", {}},
            {"MIDI File", "", {}},
            {"Preset Pack", "", {}},
        }},
        {"Export", "", {
            {"Audio", "", {
                {"WAV", "", {
                    {"44.1 kHz / 16-bit", "", {}},
                    {"44.1 kHz / 24-bit", "", {}},
                    {"48 kHz / 16-bit", "", {}},
                    {"48 kHz / 24-bit", "", {}},
                    {"96 kHz / 24-bit", "", {}},
                    {"96 kHz / 32-bit float", "", {}},
                }},
                {"FLAC", "", {
                    {"44.1 kHz", "", {}},
                    {"48 kHz", "", {}},
                    {"96 kHz", "", {}},
                }},
                {"MP3", "", {
                    {"128 kbps", "", {}},
                    {"192 kbps", "", {}},
                    {"256 kbps", "", {}},
                    {"320 kbps", "", {}},
                }},
                {"OGG Vorbis", "", {}},
            }},
            {"MIDI", "", {}},
            {"Patch Selection", "", {}},
        }},
        Sep(),
        {"Patch Settings...", "", {}},
        {"Module Browser", "Ctrl+B", {}},
        Sep(),
        {"Exit", "", {}},
    }},
    {"Edit", "", {
        {"Undo", "Ctrl+Z", {}},
        {"Redo", "Ctrl+Y", {}},
        Sep(),
        {"Cut", "Ctrl+X", {}},
        {"Copy", "Ctrl+C", {}},
        {"Paste", "Ctrl+V", {}},
        {"Paste Special", "", {
            {"Paste with Cables", "", {}},
            {"Paste at Cursor", "", {}},
            {"Paste as New Row", "", {}},
        }},
        Sep(),
        {"Select All", "Ctrl+A", {}},
        Sep(),
        {"Preferences...", "", {}},
    }},
    {"Modules", "", {
        {"Fundamental", "", {}},
        {"Befaco", "", {}},
        {"Bogaudio", "", {}},
        {"Count Modula", "", {}},
        {"dbRackModules", "", {}},
        {"Geodesics", "", {}},
        {"Grande", "", {}},
        {"HetrickCV", "", {}},
        {"Impromptu", "", {}},
        {"JW-Modules", "", {}},
        {"KautenjaDSP", "", {}},
        {"Lilac Loop", "", {}},
        {"Mindmeld", "", {}},
        {"moDllz", "", {}},
        {"Nysthi", "", {}},
        {"Ohmer", "", {}},
        {"Path Set", "", {}},
        {"Squinky Labs", "", {}},
        {"Stellare", "", {}},
        {"Stoermelder", "", {}},
        {"Submarine", "", {}},
        {"Valley", "", {}},
        {"Voxglitch", "", {}},
        {"ZZC", "", {}},
    }, false, 300},
    {"View", "", {
        {"Zoom", "", {
            {"50%", "", {}},
            {"75%", "", {}},
            {"100%", "", {}},
            {"150%", "", {}},
            {"200%", "", {}},
        }},
        {"Theme", "", {
            {"Dark", "", {}},
            {"Light", "", {}},
            {"High Contrast", "", {}},
        }},
        Sep(),
        {"Module Browser", "Ctrl+B", {}},
        {"Cable Manager", "", {}},
        Sep(),
        {"Fullscreen", "F11", {}},
    }},
    {"Help", "", {
        {"Documentation", "", {}},
        {"Keyboard Shortcuts", "", {}},
        Sep(),
        {"Check for Updates", "", {}},
        {"About", "", {}},
    }},
};

// ─── State ──────────────────────────────────────────────────────────────────

struct MenuState {
    std::string lastAction;
    int openBar = -1;                       // Which bar item's dropdown is open (-1 = closed)
    std::vector<std::string> activeItems;   // Per-depth: which item has its submenu open
};

static Store<MenuState>* g_state = nullptr;
static float g_winWidth = 1000;
static float g_winHeight = 700;

// ─── Layout constants ───────────────────────────────────────────────────────

namespace c {
    constexpr float BAR_HEIGHT = 30;
    constexpr float BAR_PAD_X = 14;
    constexpr float CHAR_WIDTH = 7.0f;
    constexpr float MENU_WIDTH = 230;
    constexpr float ITEM_HEIGHT = 26;
    constexpr float SEP_HEIGHT = 9;
    constexpr float MENU_PAD_Y = 4;
    constexpr float FONT_SIZE = 14;
    constexpr float FONT_SMALL = 12;

    constexpr uint32_t BG = 0x1a1a2eFF;
    constexpr uint32_t BAR_BG = 0x16213eFF;
    constexpr uint32_t MENU_BG = 0x252540FF;
    constexpr uint32_t HOVER_BG = 0x0f3460FF;
    constexpr uint32_t BAR_ACTIVE_BG = 0x252540FF;
    constexpr uint32_t TEXT = 0xFFFFFFFF;
    constexpr uint32_t TEXT_DIM = 0x888899FF;
    constexpr uint32_t BORDER = 0x333355FF;
    constexpr uint32_t SEP = 0x333355FF;
}

// ─── Helpers ────────────────────────────────────────────────────────────────

static float barItemWidth(const std::string& label) {
    return static_cast<float>(label.size()) * c::CHAR_WIDTH + c::BAR_PAD_X * 2;
}

static float barItemX(int index) {
    float x = 0;
    for (int i = 0; i < index; i++)
        x += barItemWidth(g_menuBar[i].label);
    return x;
}

// Y offset of item at `index` within a menu, accounting for separator heights
static float itemYOffset(const std::vector<MenuDef>& items, int index) {
    float y = 0;
    for (int i = 0; i < index; i++)
        y += items[i].separator ? c::SEP_HEIGHT : c::ITEM_HEIGHT;
    return y;
}

// Walk the menu tree to find the MenuDef whose children are shown at a given depth
static const MenuDef& resolveParent(int openBar, const std::vector<std::string>& activeItems, int depth) {
    if (depth == 0)
        return g_menuBar[openBar];

    const auto& parentDef = resolveParent(openBar, activeItems, depth - 1);
    const auto& key = activeItems[depth - 1];
    for (const auto& item : parentDef.children) {
        if (item.label == key)
            return item;
    }

    return g_menuBar[openBar];  // fallback (shouldn't happen)
}

// Walk the menu tree to find items at a given depth
static const std::vector<MenuDef>& resolveItems(int openBar, const std::vector<std::string>& activeItems, int depth) {
    return resolveParent(openBar, activeItems, depth).children;
}

// Total natural height of a menu's rows (defined below; used by placement).
static float menuContentHeight(const std::vector<MenuDef>& items);

// Find index of item with given label
static int findItemIndex(const std::vector<MenuDef>& items, const std::string& label) {
    for (int i = 0; i < static_cast<int>(items.size()); i++) {
        if (items[i].label == label)
            return i;
    }
    return 0;
}

// All edge/corner/cascade geometry comes from the library helper
// (yui/layout/Placement.hpp): placePanel for a bar dropdown, placeSubmenu for
// cascading children. This example only walks the menu tree to find each panel's
// anchor, then hands the geometry to the library.
//
// The menu bar reserves the top BAR_HEIGHT pixels, so panels must stay below it.
// The library Viewport takes a per-edge inset, so the top inset IS the bar
// height — placement clamps panels below the bar with no extra work. The bottom
// inset keeps a small gap above the window's bottom edge.

namespace lay = yui::layout;

static lay::Viewport viewport() {
    return {g_winWidth, g_winHeight,
            c::BAR_HEIGHT,  // top: reserve the menu bar
            8, 20, 8};      // right, bottom, left
}

// Natural panel content height including vertical padding.
static float panelHeight(float contentH) { return contentH + c::MENU_PAD_Y * 2; }

// Resolve the anchor for the panel at a given depth, then place it against the
// window via the library. Recurses through ancestors to find the anchor.
static lay::PlacedRect menuPlacement(int openBar, const std::vector<std::string>& activeItems,
                                     int depth, float contentH, float fixedMaxH) {
    float capH = fixedMaxH > 0 ? fixedMaxH + c::MENU_PAD_Y * 2 : 0;

    if (depth == 0) {
        // A bar dropdown opens below its bar item.
        float anchorX = barItemX(openBar);
        return lay::placePanel({anchorX, c::BAR_HEIGHT},
                               {c::MENU_WIDTH, panelHeight(contentH)}, viewport(), capH);
    }

    const auto& parentItems = resolveItems(openBar, activeItems, depth - 1);
    const MenuDef& parentDef = resolveParent(openBar, activeItems, depth - 1);
    float parentContentH = menuContentHeight(parentItems);
    auto parent = menuPlacement(openBar, activeItems, depth - 1, parentContentH, parentDef.maxHeight);

    int activeIdx = findItemIndex(parentItems, activeItems[depth - 1]);
    float anchorY = parent.y + c::MENU_PAD_Y + itemYOffset(parentItems, activeIdx);

    // Submenu opens flush to the parent (side chosen by the library, never
    // overlapping); Y anchors to the parent row and clamps onto the screen.
    lay::Rect parentRect{parent.x, parent.y, c::MENU_WIDTH, parent.height};
    return lay::placeSubmenu(parentRect, anchorY,
                             {c::MENU_WIDTH, panelHeight(contentH)}, viewport(),
                             lay::Side::Right, capH);
}

// ─── Menu item row ──────────────────────────────────────────────────────────

static VNode MenuItemRow(const MenuDef& item, int depth, const MenuState& ms) {
    bool isActive = depth < static_cast<int>(ms.activeItems.size()) && ms.activeItems[depth] == item.label;

    return Row(
        Text(item.label).fontSize(c::FONT_SIZE).color(c::TEXT).flexGrow(1),
        When(!item.shortcut.empty(),
            Text(item.shortcut).fontSize(c::FONT_SMALL).color(c::TEXT_DIM)),
        When(hasKids(item),
            Text("\xe2\x96\xb8").fontSize(c::FONT_SMALL).color(c::TEXT_DIM))  // ▸
    )
    .height(c::ITEM_HEIGHT)
    .paddingLeft(12).paddingRight(12)
    .alignItems(AlignItems::Center)
    .backgroundColor(isActive ? c::HOVER_BG : 0)
    .hoverStyle(BoxStyle{.backgroundColor = c::HOVER_BG})
    .onHover([label = item.label, depth, kids = hasKids(item)](bool h) {
        if (!h) return;
        g_state->set([label, depth, kids](MenuState& s) {
            s.activeItems.resize(depth + 1);
            s.activeItems[depth] = kids ? label : "";
        });
    })
    .onClick([label = item.label, kids = hasKids(item)]() {
        if (kids) return;
        g_state->set([label](MenuState& s) {
            s.lastAction = label;
            s.openBar = -1;
            s.activeItems.clear();
        });
    });
}

static VNode SepRow() {
    return Box()
        .height(1).marginTop(4).marginBottom(4)
        .marginLeft(8).marginRight(8)
        .backgroundColor(c::SEP);
}

// ─── Menu panel (a single dropdown or submenu) ──────────────────────────────

static float menuContentHeight(const std::vector<MenuDef>& items) {
    float h = 0;
    for (const auto& item : items)
        h += item.separator ? c::SEP_HEIGHT : c::ITEM_HEIGHT;
    return h;
}

static VNode MenuPanel(const std::vector<MenuDef>& items, int depth,
                       const lay::PlacedRect& at, const MenuState& ms) {
    std::vector<Child> rows;
    for (const auto& item : items) {
        if (item.separator) {
            rows.push_back(SepRow());
        } else {
            rows.push_back(MenuItemRow(item, depth, ms));
        }
    }

    return Box(
        Scroll(
            Column(std::move(rows))
        ).flexGrow(1)
    )
    .positionType(PositionType::Absolute)
    .positionLeft(at.x).positionTop(at.y)
    .width(c::MENU_WIDTH)
    .height(at.height)
    .backgroundColor(c::MENU_BG)
    .borderColor(c::BORDER)
    .borderWidth(1)
    .borderRadius(4)
    .paddingTop(c::MENU_PAD_Y).paddingBottom(c::MENU_PAD_Y);
}

// ─── Menu bar ───────────────────────────────────────────────────────────────

static VNode MenuBar(const MenuState& ms) {
    std::vector<Child> items;
    for (int i = 0; i < static_cast<int>(g_menuBar.size()); i++) {
        bool isOpen = ms.openBar == i;
        items.push_back(
            Box(Text(g_menuBar[i].label).fontSize(c::FONT_SIZE).color(c::TEXT))
                .height(c::BAR_HEIGHT)
                .paddingLeft(c::BAR_PAD_X).paddingRight(c::BAR_PAD_X)
                .alignItems(AlignItems::Center)
                .justifyContent(JustifyContent::Center)
                .backgroundColor(isOpen ? c::BAR_ACTIVE_BG : 0)
                .hoverStyle(BoxStyle{.backgroundColor = c::HOVER_BG})
                .onClick([i]() {
                    g_state->set([i](MenuState& s) {
                        if (s.openBar == i) {
                            s.openBar = -1;
                            s.activeItems.clear();
                        } else {
                            s.openBar = i;
                            s.activeItems.clear();
                        }
                    });
                })
                .onHover([i](bool h) {
                    if (!h) return;
                    // Switch dropdown when hovering a different bar item while menu is open
                    if (g_state->peek().openBar >= 0 && g_state->peek().openBar != i) {
                        g_state->set([i](MenuState& s) {
                            s.openBar = i;
                            s.activeItems.clear();
                        });
                    }
                })
        );
    }

    return Row(std::move(items)).backgroundColor(c::BAR_BG);
}

// ─── App ────────────────────────────────────────────────────────────────────

static Component App() {
    return [](ComponentContext&) -> VNode {
        const auto& ms = g_state->use();

        // Content area
        auto content = Column(
            MenuBar(ms),
            Box(
                Column(
                    Text("Cascading Menu Demo").fontSize(20).color(c::TEXT),
                    Gap(12),
                    Text("Click a menu bar item to open its dropdown.").fontSize(c::FONT_SIZE).color(c::TEXT_DIM),
                    Text("Hover items with \xe2\x96\xb8 to open submenus.").fontSize(c::FONT_SIZE).color(c::TEXT_DIM),
                    Text("The Modules menu scrolls \xe2\x80\x94 menus clamp to available window space.")
                        .fontSize(c::FONT_SIZE).color(c::TEXT_DIM),
                    Gap(24),
                    When(!ms.lastAction.empty(),
                        Row(
                            Text("Last action: ").fontSize(c::FONT_SIZE).color(c::TEXT_DIM),
                            Text(ms.lastAction).fontSize(c::FONT_SIZE).color(0x4ade80FF)
                        )
                    )
                ).gap(4)
            )
            .flexGrow(1)
            .justifyContent(JustifyContent::Center)
            .alignItems(AlignItems::Center)
            .backgroundColor(c::BG)
        ).flexGrow(1);

        // If no menu is open, just show content
        if (ms.openBar < 0) return content;

        // Menu is open — build layers
        std::vector<Child> layers;
        layers.push_back(std::move(content));

        // Transparent backdrop to catch clicks outside menus
        layers.push_back(
            Box()
                .positionType(PositionType::Absolute)
                .positionLeft(0).positionTop(0)
                .widthPercent(100).heightPercent(100)
                .onClick([]() {
                    g_state->set([](MenuState& s) {
                        s.openBar = -1;
                        s.activeItems.clear();
                    });
                })
        );

        // Root dropdown (depth 0)
        const auto& rootDef = resolveParent(ms.openBar, ms.activeItems, 0);
        auto rootPlace = menuPlacement(ms.openBar, ms.activeItems, 0,
                                       menuContentHeight(rootDef.children), rootDef.maxHeight);
        layers.push_back(MenuPanel(rootDef.children, 0, rootPlace, ms));

        // Cascading submenus (depth 1, 2, ...)
        for (int d = 0; d < static_cast<int>(ms.activeItems.size()); d++) {
            if (ms.activeItems[d].empty()) break;

            const auto& parentDef = resolveParent(ms.openBar, ms.activeItems, d + 1);
            if (parentDef.children.empty()) break;

            auto subPlace = menuPlacement(ms.openBar, ms.activeItems, d + 1,
                                          menuContentHeight(parentDef.children), parentDef.maxHeight);
            layers.push_back(MenuPanel(parentDef.children, d + 1, subPlace, ms));
        }

        return Box(std::move(layers)).flexGrow(1);
    };
}

// ─── Host ───────────────────────────────────────────────────────────────────

class MenuHost : public Host {
public:
    MenuHost(NVGcontext* vg, int fontId) : renderer_(vg, fontId) {
        setTextMeasurer(&renderer_);
        setRender(App());
    }

    void frame(int w, int h, float dt) {
        update(w, h, dt);
        renderer_.render(root());
    }

private:
    nvg::NvgRenderer renderer_;
};

// ─── GLFW callbacks ─────────────────────────────────────────────────────────

static MenuHost* g_host = nullptr;

static void cursorPosCallback(GLFWwindow*, double x, double y) {
    if (g_host) g_host->handleMouseMove(static_cast<float>(x), static_cast<float>(y));
}

static void mouseButtonCallback(GLFWwindow*, int button, int action, int) {
    if (!g_host) return;
    double x, y;
    glfwGetCursorPos(glfwGetCurrentContext(), &x, &y);
    auto mb = (button == GLFW_MOUSE_BUTTON_RIGHT)    ? MouseButton::Right
              : (button == GLFW_MOUSE_BUTTON_MIDDLE) ? MouseButton::Middle
                                                     : MouseButton::Left;
    if (action == GLFW_PRESS)
        g_host->handleMouseDown(static_cast<float>(x), static_cast<float>(y), mb);
    else if (action == GLFW_RELEASE)
        g_host->handleMouseUp(static_cast<float>(x), static_cast<float>(y), mb);
}

static void scrollCallback(GLFWwindow*, double xoff, double yoff) {
    if (!g_host) return;
    double x, y;
    glfwGetCursorPos(glfwGetCurrentContext(), &x, &y);
    g_host->handleScroll(static_cast<float>(x), static_cast<float>(y),
                         static_cast<float>(xoff) * 20, static_cast<float>(yoff) * 20);
}

static void charCallback(GLFWwindow*, unsigned int codepoint) {
    if (!g_host) return;
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

static void keyCallback(GLFWwindow* window, int key, int, int action, int mods) {
    if (!g_host) return;
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_ESCAPE) {
            // Close menu first; if no menu open, close window
            if (g_state->peek().openBar >= 0) {
                g_state->set([](MenuState& s) { s.openBar = -1; s.activeItems.clear(); });
                return;
            }
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return;
        } else if (key == GLFW_KEY_BACKSPACE) {
            g_host->handleEditCommand(EditCommand::DeleteBackward);
        } else if (key == GLFW_KEY_ENTER) {
            g_host->handleSubmit();
        }
    }
    uint16_t keyMod = 0;
    if (mods & GLFW_MOD_SHIFT) keyMod |= KeyMod_Shift;
    if (mods & GLFW_MOD_CONTROL) keyMod |= KeyMod_Ctrl;
    if (mods & GLFW_MOD_ALT) keyMod |= KeyMod_Alt;
    if (action == GLFW_PRESS)
        g_host->handleKeyDown(key, keyMod, false);
    else if (action == GLFW_REPEAT)
        g_host->handleKeyDown(key, keyMod, true);
    else if (action == GLFW_RELEASE)
        g_host->handleKeyUp(key, keyMod);
}

// ─── Main ───────────────────────────────────────────────────────────────────

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

    GLFWwindow* window = glfwCreateWindow(1000, 700, "Cascading Menu - yui", nullptr, nullptr);
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

    int fontId = -1;
    const char* fontPaths[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        nullptr
    };
    for (const char** p = fontPaths; *p; p++) {
        fontId = nvgCreateFont(vg, "default", *p);
        if (fontId >= 0) break;
    }
    if (fontId < 0) {
        std::cerr << "Failed to load font\n";
        nvgDeleteGL3(vg);
        glfwTerminate();
        return 1;
    }

    {
        Store<MenuState> stateStore;
        g_state = &stateStore;

        MenuHost host(vg, fontId);
        g_host = &host;

        glfwSetCursorPosCallback(window, cursorPosCallback);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
        glfwSetScrollCallback(window, scrollCallback);
        glfwSetCharCallback(window, charCallback);
        glfwSetKeyCallback(window, keyCallback);

        while (!glfwWindowShouldClose(window)) {
            int fbW, fbH;
            glfwGetFramebufferSize(window, &fbW, &fbH);
            int winW, winH;
            glfwGetWindowSize(window, &winW, &winH);
            float pxRatio = static_cast<float>(fbW) / static_cast<float>(winW);

            g_winWidth = static_cast<float>(winW);
            g_winHeight = static_cast<float>(winH);

            glViewport(0, 0, fbW, fbH);
            glClearColor(0.1f, 0.1f, 0.18f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

            nvgBeginFrame(vg, winW, winH, pxRatio);
            host.frame(winW, winH, 1.0f / 60.0f);
            nvgEndFrame(vg);

            glfwSwapBuffers(window);
            glfwPollEvents();
        }

        g_host = nullptr;
        g_state = nullptr;
    }

    nvgDeleteGL3(vg);
    glfwTerminate();
    return 0;
}
