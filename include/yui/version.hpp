#pragma once

// Version macros for yui.
//
// CMake configures version.hpp.in into the build tree (with the real
// PROJECT_VERSION) and that copy is first on the include path, so CMake
// consumers see the configured values. These #ifndef-guarded fallbacks make the
// header self-sufficient for non-CMake build paths (the Makefile and the VCV
// Rack yui.mk fragment), which never run configure_file. Keep them in sync with
// the project() VERSION in CMakeLists.txt on each release.
#ifndef YUI_VERSION_MAJOR
#define YUI_VERSION_MAJOR 1
#define YUI_VERSION_MINOR 2
#define YUI_VERSION_PATCH 0
#define YUI_VERSION "1.2.0"
#endif
