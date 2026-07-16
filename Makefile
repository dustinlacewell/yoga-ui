# yui - Declarative UI library with Yoga layout
#
# Build: make
# Test:  make test
# SDL:   make sdl_showcase
# NVG:   make nvg_showcase
# Clean: make clean

CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -I include -I deps/yoga -MMD -MP

# Detect OS
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)
ifeq ($(UNAME_S),Darwin)
    OS := macos
else ifeq ($(UNAME_S),Linux)
    OS := linux
else
    OS := windows
endif

# Build directories
BUILD := build
OBJ_DIR := $(BUILD)/obj
LIB_DIR := $(BUILD)/lib
BIN_DIR := $(BUILD)/bin

# Yoga sources (from deps submodule)
YOGA_SRC := $(wildcard deps/yoga/yoga/*.cpp) $(wildcard deps/yoga/yoga/**/*.cpp)
YOGA_OBJ := $(patsubst deps/yoga/%.cpp,$(OBJ_DIR)/yoga/%.o,$(YOGA_SRC))

# yui core sources (core + the backend-agnostic render pass)
YUI_SRC := $(wildcard src/core/*.cpp) $(wildcard src/render/*.cpp)
YUI_OBJ := $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(YUI_SRC))

# SDL backend sources
SDL_SRC := $(wildcard src/sdl/*.cpp)
SDL_OBJ := $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(SDL_SRC))

# NVG backend sources
NVG_SRC := $(wildcard src/nvg/*.cpp)
NVG_OBJ := $(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(NVG_SRC))

# Test sources (bench_reconciler carries its own main — not a doctest TU)
TEST_SRC := $(filter-out test/bench_reconciler.cpp,$(wildcard test/*.cpp))
TEST_OBJ := $(patsubst test/%.cpp,$(OBJ_DIR)/test/%.o,$(TEST_SRC))

# Example sources
SDL_EXAMPLE_OBJ := $(OBJ_DIR)/examples/sdl_showcase.o
NVG_EXAMPLE_OBJ := $(OBJ_DIR)/examples/nvg_showcase.o

# Platform-specific flags
# Note: nanovg doesn't have pkg-config, so we add common include paths manually
ifeq ($(OS),macos)
    SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_gfx 2>/dev/null)
    SDL_LIBS := $(shell pkg-config --libs sdl2 SDL2_ttf SDL2_gfx 2>/dev/null)
    NVG_CFLAGS := $(shell pkg-config --cflags glfw3 2>/dev/null) -I/opt/homebrew/include -I/opt/homebrew/include/nanovg -I/usr/local/include/nanovg
    NVG_LIBS := $(shell pkg-config --libs glfw3 2>/dev/null) -framework OpenGL -lnanovg
else ifeq ($(OS),linux)
    SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_gfx 2>/dev/null)
    SDL_LIBS := $(shell pkg-config --libs sdl2 SDL2_ttf SDL2_gfx 2>/dev/null)
    NVG_CFLAGS := $(shell pkg-config --cflags glfw3 glew 2>/dev/null) -I/usr/include/nanovg -I/usr/local/include/nanovg
    NVG_LIBS := $(shell pkg-config --libs glfw3 glew 2>/dev/null) -lnanovg -lGL
else
    # Windows (MSYS2/MinGW)
    SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_gfx 2>/dev/null)
    SDL_LIBS := $(shell pkg-config --libs sdl2 SDL2_ttf SDL2_gfx 2>/dev/null)
    NVG_CFLAGS := $(shell pkg-config --cflags glfw3 glew 2>/dev/null) -I/mingw64/include/nanovg
    NVG_LIBS := $(shell pkg-config --libs glfw3 glew 2>/dev/null) -lnanovg -lopengl32
endif

# Output
LIB := $(LIB_DIR)/libyui.a
TEST_BIN := $(BIN_DIR)/test_runner
SDL_SHOWCASE_DIR := $(BUILD)/sdl_showcase
SDL_SHOWCASE := $(SDL_SHOWCASE_DIR)/sdl_showcase
NVG_SHOWCASE_DIR := $(BUILD)/nvg_showcase
NVG_SHOWCASE := $(NVG_SHOWCASE_DIR)/nvg_showcase

.PHONY: all test clean sdl_showcase nvg_showcase

all: $(LIB)

# Create directories
$(OBJ_DIR) $(LIB_DIR) $(BIN_DIR):
	mkdir -p $@

$(OBJ_DIR)/core $(OBJ_DIR)/render $(OBJ_DIR)/sdl $(OBJ_DIR)/nvg $(OBJ_DIR)/test $(OBJ_DIR)/examples:
	mkdir -p $@

$(OBJ_DIR)/yoga/yoga $(OBJ_DIR)/yoga/yoga/algorithm $(OBJ_DIR)/yoga/yoga/config $(OBJ_DIR)/yoga/yoga/debug $(OBJ_DIR)/yoga/yoga/event $(OBJ_DIR)/yoga/yoga/node:
	mkdir -p $@

# Library
$(LIB): $(YUI_OBJ) $(YOGA_OBJ) | $(LIB_DIR)
	ar rcs $@ $^

# Core objects
$(OBJ_DIR)/core/%.o: src/core/%.cpp | $(OBJ_DIR)/core
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Render pass objects (backend-agnostic tree renderer)
$(OBJ_DIR)/render/%.o: src/render/%.cpp | $(OBJ_DIR)/render
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# SDL backend objects
$(OBJ_DIR)/sdl/%.o: src/sdl/%.cpp | $(OBJ_DIR)/sdl
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c -o $@ $<

# NVG backend objects
$(OBJ_DIR)/nvg/%.o: src/nvg/%.cpp | $(OBJ_DIR)/nvg
	$(CXX) $(CXXFLAGS) $(NVG_CFLAGS) -c -o $@ $<

# Test objects
$(OBJ_DIR)/test/%.o: test/%.cpp | $(OBJ_DIR)/test
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# SDL example objects
$(OBJ_DIR)/examples/sdl_showcase.o: examples/sdl_showcase.cpp | $(OBJ_DIR)/examples
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c -o $@ $<

# NVG example objects
$(OBJ_DIR)/examples/nvg_showcase.o: examples/nvg_showcase.cpp | $(OBJ_DIR)/examples
	$(CXX) $(CXXFLAGS) $(NVG_CFLAGS) -c -o $@ $<

# Yoga objects
$(OBJ_DIR)/yoga/yoga/%.o: deps/yoga/yoga/%.cpp | $(OBJ_DIR)/yoga/yoga
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJ_DIR)/yoga/yoga/algorithm/%.o: deps/yoga/yoga/algorithm/%.cpp | $(OBJ_DIR)/yoga/yoga/algorithm
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJ_DIR)/yoga/yoga/config/%.o: deps/yoga/yoga/config/%.cpp | $(OBJ_DIR)/yoga/yoga/config
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJ_DIR)/yoga/yoga/debug/%.o: deps/yoga/yoga/debug/%.cpp | $(OBJ_DIR)/yoga/yoga/debug
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJ_DIR)/yoga/yoga/event/%.o: deps/yoga/yoga/event/%.cpp | $(OBJ_DIR)/yoga/yoga/event
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJ_DIR)/yoga/yoga/node/%.o: deps/yoga/yoga/node/%.cpp | $(OBJ_DIR)/yoga/yoga/node
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Test binary
$(TEST_BIN): $(TEST_OBJ) $(LIB) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_OBJ) -L$(LIB_DIR) -lyui

test: $(TEST_BIN)
	./$(TEST_BIN)

# SDL showcase
$(SDL_SHOWCASE_DIR):
	mkdir -p $@

$(SDL_SHOWCASE): $(SDL_EXAMPLE_OBJ) $(SDL_OBJ) $(LIB) | $(SDL_SHOWCASE_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SDL_EXAMPLE_OBJ) $(SDL_OBJ) -L$(LIB_DIR) -lyui $(SDL_LIBS)

sdl_showcase: $(SDL_SHOWCASE)
ifeq ($(OS),windows)
	@echo "Copying DLLs for Windows..."
	@cp -u /mingw64/bin/SDL2.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/SDL2_ttf.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/SDL2_gfx.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libfreetype-6.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/zlib1.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libbz2-1.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libpng16-16.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libbrotlidec.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libbrotlicommon.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libharfbuzz-0.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libglib-2.0-0.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libintl-8.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libiconv-2.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libpcre2-8-0.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libgraphite2.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libstdc++-6.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libgcc_s_seh-1.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libwinpthread-1.dll $(SDL_SHOWCASE_DIR)/ 2>/dev/null || true
endif

# NVG showcase
$(NVG_SHOWCASE_DIR):
	mkdir -p $@

$(NVG_SHOWCASE): $(NVG_EXAMPLE_OBJ) $(NVG_OBJ) $(LIB) | $(NVG_SHOWCASE_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(NVG_EXAMPLE_OBJ) $(NVG_OBJ) -L$(LIB_DIR) -lyui $(NVG_LIBS)

nvg_showcase: $(NVG_SHOWCASE)
ifeq ($(OS),windows)
	@echo "Copying DLLs for Windows..."
	@cp -u /mingw64/bin/glfw3.dll $(NVG_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/glew32.dll $(NVG_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libstdc++-6.dll $(NVG_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libgcc_s_seh-1.dll $(NVG_SHOWCASE_DIR)/ 2>/dev/null || true
	@cp -u /mingw64/bin/libwinpthread-1.dll $(NVG_SHOWCASE_DIR)/ 2>/dev/null || true
endif

clean:
	rm -rf $(BUILD)

# Include auto-generated dependencies
-include $(OBJ_DIR)/core/*.d
-include $(OBJ_DIR)/sdl/*.d
-include $(OBJ_DIR)/nvg/*.d
-include $(OBJ_DIR)/test/*.d
-include $(OBJ_DIR)/examples/*.d
