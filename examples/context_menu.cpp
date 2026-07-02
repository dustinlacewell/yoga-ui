// Context Menu example for yui (NanoVG backend)
//
// A right-click-anywhere context menu, built specifically to exercise
// window-border collision handling:
//
//   - Right-click opens the menu at the cursor point
//   - The panel shifts UP (not shrink-and-scroll) when it would overflow the
//     bottom edge, and LEFT when it would overflow the right edge
//   - Submenus cascade right-of-parent, flipping left near the right edge
//   - Right-click in any corner to watch the menu reposition to stay on-screen
//   - Middle-click a leaf item to invoke it without closing the menu
//
// Placement is the same robust model as cascading_menu.cpp, but anchored at the
// cursor instead of a menu bar: prefer the ideal spot, clamp into the window,
// and only scroll when a menu is genuinely taller than the whole window.
//
// Build: cmake --build build --target context_menu
// Run:   ./build/bin/context_menu

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

// A deliberately tall, deeply-nested menu so every collision case is reachable.
static const std::vector<MenuDef> g_contextMenu = {
    {"Cut", "Ctrl+X", {}},
    {"Copy", "Ctrl+C", {}},
    {"Paste", "Ctrl+V", {}},
    {"Paste Special", "", {
        {"Paste as Text", "", {}},
        {"Paste with Formatting", "", {}},
        {"Paste and Match Style", "", {}},
    }},
    Sep(),
    {"Insert", "", {
        {"Image", "", {}},
        {"Link", "", {}},
        {"Table", "", {
            {"2 x 2", "", {}},
            {"3 x 3", "", {}},
            {"4 x 4", "", {}},
            {"Custom...", "", {}},
        }},
        {"Symbol", "", {
            {"Arrow", "", {}},
            {"Currency", "", {}},
            {"Math", "", {}},
            {"Emoji", "", {}},
        }},
        {"Horizontal Rule", "", {}},
    }},
    {"Format", "", {
        {"Bold", "Ctrl+B", {}},
        {"Italic", "Ctrl+I", {}},
        {"Underline", "Ctrl+U", {}},
        Sep(),
        {"Text Color", "", {
            {"Black", "", {}},
            {"Red", "", {}},
            {"Green", "", {}},
            {"Blue", "", {}},
            {"Yellow", "", {}},
            {"Custom...", "", {}},
        }},
        {"Heading", "", {
            {"H1", "", {}},
            {"H2", "", {}},
            {"H3", "", {}},
            {"H4", "", {}},
            {"H5", "", {}},
            {"H6", "", {}},
        }},
    }},
    Sep(),
    // A long flat list (capped + scrolling) to test the taller-than-window case.
    {"Move to Layer", "", {
        {"Layer 1", "", {}},
        {"Layer 2", "", {}},
        {"Layer 3", "", {}},
        {"Layer 4", "", {}},
        {"Layer 5", "", {}},
        {"Layer 6", "", {}},
        {"Layer 7", "", {}},
        {"Layer 8", "", {}},
        {"Layer 9", "", {}},
        {"Layer 10", "", {}},
        {"Layer 11", "", {}},
        {"Layer 12", "", {}},
        {"Layer 13", "", {}},
        {"Layer 14", "", {}},
        {"Layer 15", "", {}},
        {"Layer 16", "", {}},
        {"Layer 17", "", {}},
        {"Layer 18", "", {}},
        {"Layer 19", "", {}},
        {"Layer 20", "", {}},
    }, false, 280},
    Sep(),
    {"Select All", "Ctrl+A", {}},
    {"Delete", "Del", {}},
    Sep(),
    {"Properties...", "", {}},
};

// ─── State ──────────────────────────────────────────────────────────────────

struct MenuState {
    std::string lastAction;
    bool open = false;                      // Is the context menu showing?
    float anchorX = 0, anchorY = 0;         // Cursor point where it was opened
    std::vector<std::string> activeItems;   // Per-depth: which item has its submenu open
};

static Store<MenuState>* g_state = nullptr;
static float g_winWidth = 1000;
static float g_winHeight = 700;

// ─── Layout constants ───────────────────────────────────────────────────────

namespace c {
    constexpr float CHAR_WIDTH = 7.0f;
    constexpr float MENU_WIDTH = 220;
    constexpr float ITEM_HEIGHT = 26;
    constexpr float SEP_HEIGHT = 9;
    constexpr float MENU_PAD_Y = 4;
    constexpr float SCREEN_MARGIN = 8;      // Keep menus this far from every edge
    constexpr float FONT_SIZE = 14;
    constexpr float FONT_SMALL = 12;

    constexpr uint32_t BG = 0x1a1a2eFF;
    constexpr uint32_t MENU_BG = 0x252540FF;
    constexpr uint32_t HOVER_BG = 0x0f3460FF;
    constexpr uint32_t TEXT = 0xFFFFFFFF;
    constexpr uint32_t TEXT_DIM = 0x888899FF;
    constexpr uint32_t BORDER = 0x333355FF;
    constexpr uint32_t SEP = 0x333355FF;
    constexpr uint32_t ACCENT = 0x4ade80FF;
    constexpr uint32_t CROSSHAIR = 0x4a4a6aFF;
}

// ─── Helpers ────────────────────────────────────────────────────────────────

// Total natural height of a menu's rows (defined below; used by placement).
static float menuContentHeight(const std::vector<MenuDef>& items);

// Y offset of item at `index` within a menu, accounting for separator heights
static float itemYOffset(const std::vector<MenuDef>& items, int index) {
    float y = 0;
    for (int i = 0; i < index; i++)
        y += items[i].separator ? c::SEP_HEIGHT : c::ITEM_HEIGHT;
    return y;
}

// Walk the menu tree to find the MenuDef whose children are shown at a given depth
static const MenuDef& resolveParent(const std::vector<std::string>& activeItems, int depth) {
    // Depth 0 is the root context menu; we model it with a synthetic parent.
    static const MenuDef root{"", "", g_contextMenu};
    if (depth == 0)
        return root;

    const auto& parentDef = resolveParent(activeItems, depth - 1);
    const auto& key = activeItems[depth - 1];
    for (const auto& item : parentDef.children) {
        if (item.label == key)
            return item;
    }
    return root;  // fallback (shouldn't happen)
}

static const std::vector<MenuDef>& resolveItems(const std::vector<std::string>& activeItems, int depth) {
    return resolveParent(activeItems, depth).children;
}

static int findItemIndex(const std::vector<MenuDef>& items, const std::string& label) {
    for (int i = 0; i < static_cast<int>(items.size()); i++) {
        if (items[i].label == label)
            return i;
    }
    return 0;
}

// ─── Placement ──────────────────────────────────────────────────────────────
//
// All edge/corner/cascade geometry comes from the library helper
// (yui/layout/Placement.hpp): placePanel for the cursor-anchored root,
// placeSubmenu for cascading children. This example only walks the menu tree to
// find each panel's anchor, then hands the geometry to the library.
//
// A real app with scrollable menus would anchor a submenu's Y to the parent
// row's DRAWN position (e.g. the cursor Y on hover). This demo's panels are
// placed straight from the model with no scroll offset, so the anchor is the
// content-relative row top — simpler, and correct for the non-scrolling case.

namespace lay = yui::layout;

static lay::Viewport viewport() {
    return lay::Viewport::uniform(g_winWidth, g_winHeight, c::SCREEN_MARGIN);
}

// Natural panel content height including vertical padding.
static float panelHeight(float contentH) { return contentH + c::MENU_PAD_Y * 2; }

// Resolve the anchor for the panel at a given depth, then place it against the
// window via the library. Recurses through ancestors to find the anchor.
static lay::PlacedRect menuPlacement(const MenuState& ms, int depth,
                                     float contentH, float fixedMaxH) {
    float capH = fixedMaxH > 0 ? fixedMaxH + c::MENU_PAD_Y * 2 : 0;

    if (depth == 0) {
        // Root opens at the cursor. If it would overflow the right edge, open it
        // so its right edge sits at the cursor instead (flip the anchor X).
        float anchorX = ms.anchorX;
        if (anchorX + c::MENU_WIDTH > g_winWidth - c::SCREEN_MARGIN)
            anchorX = ms.anchorX - c::MENU_WIDTH;
        return lay::placePanel({anchorX, ms.anchorY},
                               {c::MENU_WIDTH, panelHeight(contentH)}, viewport(), capH);
    }

    const auto& parentItems = resolveItems(ms.activeItems, depth - 1);
    const MenuDef& parentDef = resolveParent(ms.activeItems, depth - 1);
    float parentContentH = menuContentHeight(parentItems);
    auto parent = menuPlacement(ms, depth - 1, parentContentH, parentDef.maxHeight);

    int activeIdx = findItemIndex(parentItems, ms.activeItems[depth - 1]);
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
            s.open = false;
            s.activeItems.clear();
        });
    })
    // Middle-click invokes the action but leaves the menu open, so several
    // items can be fired in a row (demonstrates onMiddleClick).
    .onMiddleClick([label = item.label, kids = hasKids(item)]() {
        if (kids) return;
        g_state->set([label](MenuState& s) { s.lastAction = label + " (kept open)"; });
    });
}

static VNode SepRow() {
    return Box()
        .height(1).marginTop(4).marginBottom(4)
        .marginLeft(8).marginRight(8)
        .backgroundColor(c::SEP);
}

// ─── Menu panel ─────────────────────────────────────────────────────────────

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

// ─── Background canvas (crosshair at the last click point) ───────────────────

static VNode ClickMarker(const MenuState& ms) {
    if (!ms.open) return Box();  // nothing to draw

    // A thin crosshair so you can see exactly where the menu was anchored.
    return Box(
        Box()  // vertical line
            .positionType(PositionType::Absolute)
            .positionLeft(ms.anchorX).positionTop(0)
            .width(1).heightPercent(100)
            .backgroundColor(c::CROSSHAIR),
        Box()  // horizontal line
            .positionType(PositionType::Absolute)
            .positionLeft(0).positionTop(ms.anchorY)
            .widthPercent(100).height(1)
            .backgroundColor(c::CROSSHAIR)
    )
    .positionType(PositionType::Absolute)
    .positionLeft(0).positionTop(0)
    .widthPercent(100).heightPercent(100);
}

// ─── App ────────────────────────────────────────────────────────────────────

static Component App() {
    return [](ComponentContext&) -> VNode {
        const auto& ms = g_state->use();

        auto content = Box(
            Column(
                Text("Context Menu Demo").fontSize(22).color(c::TEXT),
                Gap(14),
                Text("Right-click anywhere to open the menu at the cursor.")
                    .fontSize(c::FONT_SIZE).color(c::TEXT_DIM),
                Text("Try the corners and edges \xe2\x80\x94 the menu repositions to stay on-screen.")
                    .fontSize(c::FONT_SIZE).color(c::TEXT_DIM),
                Text("Hover items with \xe2\x96\xb8 to cascade submenus.")
                    .fontSize(c::FONT_SIZE).color(c::TEXT_DIM),
                Text("Left-click invokes an item; middle-click invokes it but keeps the menu open.")
                    .fontSize(c::FONT_SIZE).color(c::TEXT_DIM),
                Text("\"Move to Layer\" is taller than the window \xe2\x80\x94 it scrolls.")
                    .fontSize(c::FONT_SIZE).color(c::TEXT_DIM),
                Gap(24),
                When(!ms.lastAction.empty(),
                    Row(
                        Text("Last action: ").fontSize(c::FONT_SIZE).color(c::TEXT_DIM),
                        Text(ms.lastAction).fontSize(c::FONT_SIZE).color(c::ACCENT)
                    ))
            ).gap(4)
        )
        .flexGrow(1)
        .justifyContent(JustifyContent::Center)
        .alignItems(AlignItems::Center)
        .backgroundColor(c::BG);

        if (!ms.open) return content;

        // Menu is open — build layers over the content.
        std::vector<Child> layers;
        layers.push_back(std::move(content));
        layers.push_back(ClickMarker(ms));

        // Transparent backdrop: left-click outside closes the menu. (A right
        // click anywhere is handled at the GLFW layer to re-anchor instead.)
        layers.push_back(
            Box()
                .positionType(PositionType::Absolute)
                .positionLeft(0).positionTop(0)
                .widthPercent(100).heightPercent(100)
                .onClick([]() {
                    g_state->set([](MenuState& s) {
                        s.open = false;
                        s.activeItems.clear();
                    });
                })
        );

        // Root menu (depth 0), anchored at the cursor.
        const auto& rootItems = g_contextMenu;
        auto rootPlace = menuPlacement(ms, 0, menuContentHeight(rootItems), 0);
        layers.push_back(MenuPanel(rootItems, 0, rootPlace, ms));

        // Cascading submenus (depth 1, 2, ...).
        for (int d = 0; d < static_cast<int>(ms.activeItems.size()); d++) {
            if (ms.activeItems[d].empty()) break;

            const auto& parentDef = resolveParent(ms.activeItems, d + 1);
            if (parentDef.children.empty()) break;

            auto subPlace = menuPlacement(ms, d + 1, menuContentHeight(parentDef.children), parentDef.maxHeight);
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

static void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    if (!g_host) return;
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    float fx = static_cast<float>(x), fy = static_cast<float>(y);

    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        // Open (or re-anchor) the context menu at the cursor.
        g_state->set([fx, fy](MenuState& s) {
            s.open = true;
            s.anchorX = fx;
            s.anchorY = fy;
            s.activeItems.clear();
        });
        return;
    }

    auto mb = (button == GLFW_MOUSE_BUTTON_RIGHT)    ? MouseButton::Right
              : (button == GLFW_MOUSE_BUTTON_MIDDLE) ? MouseButton::Middle
                                                     : MouseButton::Left;
    if (action == GLFW_PRESS)
        g_host->handleMouseDown(fx, fy, mb);
    else if (action == GLFW_RELEASE)
        g_host->handleMouseUp(fx, fy, mb);
}

static void scrollCallback(GLFWwindow* window, double xoff, double yoff) {
    if (!g_host) return;
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    g_host->handleScroll(static_cast<float>(x), static_cast<float>(y),
                         static_cast<float>(xoff) * 20, static_cast<float>(yoff) * 20);
}

static void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (!g_host) return;
    if ((action == GLFW_PRESS || action == GLFW_REPEAT) && key == GLFW_KEY_ESCAPE) {
        // Close the menu first; if nothing is open, close the window.
        if (g_state->peek().open) {
            g_state->set([](MenuState& s) { s.open = false; s.activeItems.clear(); });
            return;
        }
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
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

    GLFWwindow* window = glfwCreateWindow(1000, 700, "Context Menu - yui", nullptr, nullptr);
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
