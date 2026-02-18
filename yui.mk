# yui.mk — Makefile fragment for VCV Rack plugin consumers
#
# Usage in your plugin Makefile (before including plugin.mk):
#   include dep/yui/yui.mk

YUI_DIR ?= dep/yui

# Include paths
FLAGS += -I$(YUI_DIR)/include -I$(YUI_DIR)/deps/yoga

# Yoga layout engine
SOURCES += $(wildcard $(YUI_DIR)/deps/yoga/yoga/*.cpp)
SOURCES += $(wildcard $(YUI_DIR)/deps/yoga/yoga/**/*.cpp)

# YUI core
SOURCES += $(YUI_DIR)/src/core/Component.cpp
SOURCES += $(YUI_DIR)/src/core/EventHandler.cpp
SOURCES += $(YUI_DIR)/src/core/Fiber.cpp
SOURCES += $(YUI_DIR)/src/core/Node.cpp
SOURCES += $(YUI_DIR)/src/core/Reconciler.cpp
SOURCES += $(YUI_DIR)/src/core/VNode.cpp

# YUI NanoVG renderer (for VCV Rack / NanoVG hosts)
SOURCES += $(YUI_DIR)/src/nvg/NvgRenderer.cpp
