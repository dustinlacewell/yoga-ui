# Getting Started with yui

yui is a declarative, flexbox-based UI library for modern C++. If you've used React, Flutter, or SwiftUI, you'll feel right at home — yui brings the same component-driven, reactive paradigm to C++20.

```cpp
Component App() {
    return [](ComponentContext& ctx) -> VNode {
        auto [name, setName] = ctx.useState<std::string>("World");

        return Column({
            Text("Hello, " + name + "!").fontSize(24).color(0xFFFFFFFF),
            Text("Build beautiful UIs with C++").color(0xAAAAAAFF),
        }).gap(8).padding(20).alignItems(AlignItems::Center);
    };
}
```

This guide will walk you through everything you need to build your first yui application.

## Requirements

- **C++20 compiler**: GCC 10+, Clang 12+, or MSVC 2019+
- **CMake 3.16+**
- **A rendering backend**: SDL2 or NanoVG (we'll cover both)

## Quick Start

### 1. Clone the Repository

```bash
git clone --recursive https://github.com/user/yui.git
cd yui
```

The `--recursive` flag is important — it pulls in the Yoga layout engine and NanoVG submodules.

### 2. Install Backend Dependencies

Choose a rendering backend. SDL2 is simpler to set up; NanoVG offers smoother graphics.

**Ubuntu/Debian:**
```bash
# SDL2 backend
sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-gfx-dev

# NanoVG backend
sudo apt install libglfw3-dev libglew-dev
```

**macOS:**
```bash
# SDL2 backend
brew install sdl2 sdl2_ttf sdl2_gfx

# NanoVG backend
brew install glfw glew
```

**Windows (MSYS2/MinGW):**
```bash
# SDL2 backend
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_ttf mingw-w64-x86_64-SDL2_gfx

# NanoVG backend
pacman -S mingw-w64-x86_64-glfw mingw-w64-x86_64-glew
```

### 3. Build and Run

```bash
cmake -B build
cmake --build build --target hello_world
./build/bin/hello_world
```

You should see a window with "Hello, World!" centered on screen.

---

## Core Concepts

### Primitives

yui provides five primitive node types — the building blocks of every UI:

| Primitive | Purpose | Example |
|-----------|---------|---------|
| `Box` | Layout container (like HTML `<div>`) | `Box({ child1, child2 })` |
| `Text` | Display text | `Text("Hello")` |
| `Input` | Text input field | `Input().value(text)` |
| `Scroll` | Scrollable container | `Scroll(content)` |
| `Canvas` | Custom drawing | `Canvas(drawFn)` |

### Components

There are two kinds of components:

**Helper functions** return VNodes directly. Simple, no state:

```cpp
VNode Button(std::string label, std::function<void()> onClick) {
    return Box({
        Text(label).color(0xFFFFFFFF)
    })
    .padding(12)
    .backgroundColor(0x3366FFFF)
    .borderRadius(6)
    .hoverStyle(BoxStyle{.backgroundColor = 0x4477FFFF})
    .onClick(onClick);
}
```

**Stateful components** use `Component()` with hooks for local state, effects, and selective re-rendering:

```cpp
Component ClickCounter() {
    return [](ComponentContext& ctx) -> VNode {
        auto [count, setCount] = ctx.useState(0);

        return Box(Text("Clicks: " + std::to_string(count)))
            .padding(12)
            .onClick([=] { setCount(count + 1); });
    };
}
```

Both are first-class children — mix freely:

```cpp
Column({
    Text("Welcome!"),              // VNode (primitive)
    Button("Press me", handler),   // VNode (helper function)
    ClickCounter(),                // Component (stateful)
})
```

### The Fluent API

Every node supports a fluent API for setting properties. Chain methods to configure layout, styling, and behavior:

```cpp
Box({ ... })
    // Layout
    .width(200).height(100)
    .padding(16).margin(8).gap(12)
    .flexGrow(1)
    .flexDirection(FlexDirection::Row)
    .justifyContent(JustifyContent::Center)
    .alignItems(AlignItems::Center)

    // Styling
    .backgroundColor(0x1a1a1aFF)
    .borderColor(0x404040FF)
    .borderWidth(1)
    .borderRadius(8)

    // Events
    .onClick([] { /* handle click */ })
    .onHover([](bool hovered) { /* handle hover */ })

    // State-based styles
    .hoverStyle(BoxStyle{.backgroundColor = 0x2a2a2aFF})
    .focusStyle(BoxStyle{.borderColor = 0x4a9fffFF});
```

### Layout with Flexbox

yui uses [Yoga](https://yogalayout.dev/), Facebook's flexbox implementation, for layout. If you know CSS flexbox, you know yui layout:

```cpp
// Horizontal row with centered items
Row({ a, b, c })
    .justifyContent(JustifyContent::SpaceBetween)
    .alignItems(AlignItems::Center)

// Vertical column with gaps
Column({ header, content, footer })
    .gap(16)

// Flexible sizing
Row({
    Box(sidebar).width(200),           // Fixed width
    Box(content).flexGrow(1),          // Takes remaining space
})

// Absolute positioning
Box(overlay)
    .positionType(PositionType::Absolute)
    .positionTop(0).positionLeft(0).positionRight(0)
```

**Layout helpers:**
- `Row({...})` — Horizontal flex container
- `Column({...})` — Vertical flex container
- `Spacer()` — Flexible space that grows to fill
- `Gap(size)` — Fixed-size spacing

---

## Your First App

Let's build a counter app using stateful components. Create a new file `counter.cpp`:

```cpp
#include "yui/yui.hpp"
#include "yui/sdl/sdl.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

using namespace yui;

// Stateful counter component
Component Counter() {
    return [](ComponentContext& ctx) -> VNode {
        auto [count, setCount] = ctx.useState(0);

        return Column({
            Text("Count: " + std::to_string(count))
                .fontSize(32)
                .color(0xFFFFFFFF),

            Row({
                Box(Text("-").fontSize(24).color(0xFFFFFFFF))
                    .padding(16)
                    .backgroundColor(0xDD4444FF)
                    .borderRadius(8)
                    .onClick([=] { setCount(count - 1); }),

                Gap(16),

                Box(Text("+").fontSize(24).color(0xFFFFFFFF))
                    .padding(16)
                    .backgroundColor(0x44AA44FF)
                    .borderRadius(8)
                    .onClick([=] { setCount(count + 1); }),
            }),
        })
        .gap(24)
        .padding(32)
        .alignItems(AlignItems::Center)
        .justifyContent(JustifyContent::Center)
        .flexGrow(1)
        .backgroundColor(0x1a1a2eFF);
    };
}

class CounterHost : public Host {
public:
    CounterHost(SDL_Renderer* r, const std::string& fontPath)
        : renderer_(r, fontPath, 16) {
        renderer_.registerMeasureFunc();
        setRender(Counter());
    }

    void frame(int w, int h) {
        update(w, h, 1.0f / 60.0f);
        renderer_.render(root());
    }

private:
    sdl::SdlRenderer renderer_;
};

int main() {
    if (!sdl::initSDL()) return 1;

    SDL_Window* window = SDL_CreateWindow(
        "Counter", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        400, 300, SDL_WINDOW_SHOWN
    );
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    // Find a system font
    std::string fontPath;
    const char* fonts[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        nullptr
    };
    for (const char** p = fonts; *p; p++) {
        if (TTF_OpenFont(*p, 16)) { fontPath = *p; break; }
    }

    CounterHost host(renderer, fontPath);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                running = false;

            // Forward mouse events to yui
            if (event.type == SDL_MOUSEBUTTONDOWN)
                host.handleMouseDown(event.button.x, event.button.y, MouseButton::Left);
            if (event.type == SDL_MOUSEBUTTONUP)
                host.handleMouseUp(event.button.x, event.button.y, MouseButton::Left);
            if (event.type == SDL_MOUSEMOTION)
                host.handleMouseMove(event.motion.x, event.motion.y);
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
```

**Key points:**
- `Counter()` returns a `Component` — a deferred render function with hook access
- `useState(0)` creates local state; the setter `setCount` triggers a re-render of just this component
- `Host::setRender()` accepts either a `Component` or a `std::function<VNode()>`

---

## Reactive State with Store

`Store<T>` is yui's reactive state container for shared/global state. When a Store changes, subscribed components automatically re-render:

```cpp
// Define state outside your components
Store<int> counter(0);
Store<std::string> username("");
Store<std::vector<Todo>> todos;
```

**Inside a stateful component**, `use()` subscribes only that component — other components don't re-render:

```cpp
auto CounterDisplay = [&](ComponentContext& ctx) -> VNode {
    int n = counter.use();  // Subscribe just this component
    return Text("Count: " + std::to_string(n));
};

auto StaticLabel = [](ComponentContext& ctx) -> VNode {
    return Text("I never re-render");  // No subscription, never re-renders
};
```

**In the top-level render function**, `use()` subscribes the entire host:

```cpp
host.setRender(std::function<VNode()>([&]() {
    int n = counter.use();  // Subscribes the whole host (full re-render on change)
    return Column({ Text(std::to_string(n)) });
}));
```

**Modify state from anywhere:**
```cpp
counter.set(42);                              // Replace value
counter.set([](int& n) { n++; });            // Mutate in place
todos.set([](auto& t) { t.push_back(...); }); // Works with vectors
```

**Best practices:**
- Prefer component-level `use()` for selective re-rendering
- Use `peek()` to read without subscribing (for event handlers)
- Use `set()` with a lambda for complex updates
- For component-local state, prefer `useState` over Store

---

## Handling Events

### Mouse Events

```cpp
Box(Text("Click me"))
    .onClick([] {
        std::cout << "Left clicked!\n";
    })
    .onRightClick([] {
        std::cout << "Right clicked!\n";
    })
    .onHover([](bool hovered) {
        std::cout << (hovered ? "Mouse entered" : "Mouse left") << "\n";
    });
```

### Keyboard Events

```cpp
Box(content)
    .onKeyDown([](int keyCode, uint16_t modifiers) {
        if (modifiers & KeyMod_Ctrl && keyCode == 'S') {
            save();
        }
    });
```

### Input Fields

Input uses a controlled pattern — you set the value and handle changes:

```cpp
// In a stateful component:
auto [email, setEmail] = ctx.useState<std::string>("");

Input()
    .value(email)
    .placeholder("you@example.com")
    .fontSize(14)
    .padding(12)
    .borderRadius(4)
    .backgroundColor(0x333333FF)
    .onChange([=](const std::string& value) {
        setEmail(value);
    })
    .onSubmit([=] {
        submit(email);
    });
```

Or use `useField` for two-way Store binding:

```cpp
auto [email, setEmail] = ctx.useField(formStore, &FormState::email);

Input()
    .value(email)
    .onChange([=](const std::string& v) { setEmail(v); });
```

---

## Conditional Rendering

```cpp
// Show only if condition is true
When(isLoggedIn, UserProfile())

// Choose between two alternatives
If(isLoading,
    Spinner(),
    Content()
)

// Both preserve position for stable reconciliation
Column({
    Header(),
    When(showBanner, PromoBanner()),  // Space reserved even when hidden
    MainContent(),
})
```

---

## Rendering Lists

For dynamic lists, use the `List` helper with keys for efficient updates:

```cpp
struct Todo {
    int id;
    std::string text;
    bool done;
};

VNode TodoList(const std::vector<Todo>& todos) {
    return List(todos,
        // Key function - returns unique identifier
        [](const Todo& t) { return t.id; },

        // Render function - returns VNode for each item
        [](const Todo& t) {
            return Row({
                Text(t.text)
                    .color(t.done ? 0x888888FF : 0xFFFFFFFF)
                    .flexGrow(1),
                Text(t.done ? "done" : "todo")
                    .onClick([id = t.id] { toggleTodo(id); }),
            }).padding(8);
        }
    );
}

// Horizontal lists
HList(tabs,
    [](const Tab& t) { return t.id; },
    [](const Tab& t) { return TabButton(t); }
)
```

Render functions can return either `VNode` or `Component` — use `Component` when list items need their own local state.

**Why keys matter:** Keys help yui's reconciler identify which items changed, were added, or removed. Without keys, the entire list re-renders. With keys, only changed items update.

---

## State-Based Styles

Apply visual changes on hover or focus without manual state tracking:

```cpp
// Button with hover effect
Box(Text("Submit"))
    .backgroundColor(0x3366FFFF)
    .hoverStyle(BoxStyle{
        .backgroundColor = 0x4477FFFF,
        .borderColor = 0x88AAFFFF
    })

// Input with focus indicator
Input()
    .value(text)
    .borderColor(0x444444FF)
    .borderWidth(1)
    .hoverStyle(InputStyle{.borderColor = 0x666666FF})
    .focusStyle(InputStyle{.borderColor = 0x4a9fffFF})
```

Style structs use `std::optional` — only set the properties you want to override.

---

## Custom Drawing with Canvas

For graphics, games, or custom visualizations, use the `Canvas` primitive:

```cpp
VNode ProgressRing(float progress) {
    return Canvas([progress](void* ctx, float w, float h) {
        auto* vg = static_cast<NVGcontext*>(ctx);

        float cx = w / 2, cy = h / 2;
        float radius = std::min(w, h) / 2 - 4;

        // Background circle
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, radius);
        nvgStrokeColor(vg, nvgRGBA(60, 60, 60, 255));
        nvgStrokeWidth(vg, 4);
        nvgStroke(vg);

        // Progress arc
        float startAngle = -NVG_PI / 2;
        float endAngle = startAngle + progress * NVG_PI * 2;
        nvgBeginPath(vg);
        nvgArc(vg, cx, cy, radius, startAngle, endAngle, NVG_CW);
        nvgStrokeColor(vg, nvgRGBA(74, 222, 128, 255));
        nvgStrokeWidth(vg, 4);
        nvgLineCap(vg, NVG_ROUND);
        nvgStroke(vg);
    }).width(64).height(64);
}
```

The draw function receives:
- `ctx` — Renderer-specific context (NVGcontext* for NanoVG, SDL_Renderer* for SDL)
- `w`, `h` — Canvas dimensions after layout

---

## Scrollable Content

Wrap content in `Scroll` for scrollable containers:

```cpp
Scroll(
    Column({
        // Many items that exceed container height
        ...items
    })
)
.width(300)
.height(400)
.backgroundColor(0x1a1a1aFF)
```

Scroll handles mouse wheel events automatically. Content that exceeds bounds is clipped and scrollable.

---

## Project Integration

### Option 1: CMake FetchContent (Recommended)

```cmake
include(FetchContent)

FetchContent_Declare(
    yui
    GIT_REPOSITORY https://github.com/user/yui.git
    GIT_TAG        v0.1.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(yui)

target_link_libraries(your_app PRIVATE yui::yui)
```

### Option 2: Git Submodule

```bash
git submodule add https://github.com/user/yui.git deps/yui
git submodule update --init --recursive
```

```cmake
add_subdirectory(deps/yui)
target_link_libraries(your_app PRIVATE yui)
```

### Option 3: System Install

```bash
git clone --recursive https://github.com/user/yui.git
cd yui
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

```cmake
find_package(yui REQUIRED)
target_link_libraries(your_app PRIVATE yui::yui)
```

---

## Choosing a Backend

### SDL2 Backend

Best for: Simple apps, games with 2D graphics, cross-platform desktop apps.

```cpp
#include "yui/sdl/sdl.hpp"
#include "yui/sdl/SdlRenderer.hpp"

yui::sdl::SdlRenderer renderer(sdlRenderer, "path/to/font.ttf", 16);
renderer.registerMeasureFunc();
```

**Dependencies:** SDL2, SDL2_ttf, SDL2_gfx

### NanoVG Backend

Best for: Polished UIs, smooth animations, high-DPI displays, hardware acceleration.

```cpp
#include "yui/nvg/nvg.hpp"
#include "yui/nvg/NvgRenderer.hpp"

int fontId = nvgCreateFont(vg, "default", "path/to/font.ttf");
yui::nvg::NvgRenderer renderer(vg, fontId);
renderer.registerMeasureFunc();
```

**Dependencies:** GLFW3, GLEW, OpenGL

---

## Example Applications

The `examples/` directory contains complete applications demonstrating yui's capabilities:

| Example | Description | Build Target |
|---------|-------------|--------------|
| `hello_world.cpp` | Minimal "Hello World" | `hello_world` |
| `sdl_showcase.cpp` | SDL2 feature showcase | `sdl_showcase` |
| `nvg_showcase.cpp` | NanoVG feature showcase | `nvg_showcase` |
| `todo_app.cpp` | Complete todo list app | `todo_app` |
| `pong.cpp` | Pong game with Canvas | `pong` |
| `benchmark.cpp` | Performance stress test | `benchmark` |

Build all examples:
```bash
cmake --build build --target all_examples
```

---

## Architecture Overview

Understanding yui's architecture helps you write efficient code:

```
┌──────────────────────────────────────────────────────────────────────┐
│                           Each Frame                                 │
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   Store.set() ───► marks dirty (component or host)                   │
│                                                                      │
│   Host::update()                                                     │
│     1. Re-render dirty components (selective)                        │
│     2. Full reconcile (if host dirty):                               │
│        render() ──► VNode tree ──► Reconciler                        │
│                                     ├─► Fiber tree (state, hooks)    │
│                                     └─► Render tree (layout, draw)   │
│     3. Layout (Yoga) on render tree                                  │
│     4. Renderer draws render tree                                    │
│     5. EventHandler dispatches to render tree nodes                  │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

**VNode** — Lightweight description of desired UI. Created fresh each frame.

**Component** — Deferred render function with access to hooks (useState, useEffect, etc.).

**Fiber** — Internal tree tracking component identity, state, and subscriptions.

**Node** — Actual widget instance. Persists across frames. Holds layout and hover/focus state.

**Reconciler** — Diffs VNode/Component tree against existing Fiber+Node trees. Reuses nodes where possible, preserving state.

---

## Next Steps

Now that you understand the basics, explore these resources:

- **[Primitives Reference](primitives.md)** — Complete API for Box, Text, Input, Scroll, Canvas
- **[Components Guide](components.md)** — Hooks, Store subscriptions, and composition patterns
- **[Architecture Deep Dive](architecture.md)** — Dual-tree architecture, reconciliation, selective re-rendering
- **[Extending yui](extending.md)** — Adding custom primitives

Happy building!
