# yui - Declarative UI library with Yoga layout
#
# Build library only:
#   cmake -B build
#   cmake --build build
#
# Build specific targets:
#   cmake --build build --target test_runner    # Build tests
#   cmake --build build --target sdl_showcase   # Build SDL2 showcase
#   cmake --build build --target nvg_showcase   # Build NanoVG showcase
#   cmake --build build --target format         # Run clang-format
#
# Run tests:
#   ctest --test-dir build
#
# Options:
#   -DBUILD_TESTING=ON           Enable test target (default: ON)
#   -DBUILD_SDL_SHOWCASE=ON      Enable SDL2 showcase target (auto-enabled if deps found)
#   -DBUILD_NVG_SHOWCASE=ON      Enable NanoVG showcase target (auto-enabled if deps found)
#   -DYUI_AUTO_FIND_BACKENDS=OFF Disable auto-detection of showcase dependencies

cmake_minimum_required(VERSION 3.16)
project(yui
    VERSION 1.2.1
    DESCRIPTION "Declarative UI library with Yoga layout"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Generate version header
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/yui/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/yui/version.hpp
    @ONLY
)

# Build options
option(BUILD_TESTING "Build tests" ON)
option(BUILD_SDL_SHOWCASE "Build SDL2 showcase example" OFF)
option(BUILD_NVG_SHOWCASE "Build NanoVG showcase example" OFF)
option(YUI_AUTO_FIND_BACKENDS "Auto-enable showcases if dependencies found" ON)

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# -----------------------------------------------------------------------------
# Yoga layout engine (submodule)
# -----------------------------------------------------------------------------
add_subdirectory(deps/yoga/yoga)

# -----------------------------------------------------------------------------
# yui core library
# -----------------------------------------------------------------------------
file(GLOB YUI_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/*.cpp
)

add_library(yui STATIC ${YUI_CORE_SOURCES})

target_include_directories(yui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/yoga
)

target_link_libraries(yui PUBLIC yogacore)

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
if(BUILD_TESTING)
    enable_testing()

    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp
    )
    # Exclude standalone benchmark from test sources
    list(FILTER TEST_SOURCES EXCLUDE REGEX "bench_reconciler\\.cpp$")

    add_executable(test_runner  ${TEST_SOURCES})

    target_include_directories(test_runner PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/test
    )

    target_link_libraries(test_runner PRIVATE yui)

    add_test(NAME yui_tests COMMAND test_runner)

    # Headless performance benchmark (excluded from default build, run: cmake --build build --target bench_reconciler)
    add_executable(bench_reconciler EXCLUDE_FROM_ALL test/bench_reconciler.cpp)
    target_link_libraries(bench_reconciler PRIVATE yui)
endif()

# -----------------------------------------------------------------------------
# SDL2 Showcase (optional)
# -----------------------------------------------------------------------------
# Try find_package first (works in IDEs), then pkg-config (works in MSYS2)
find_package(SDL2 QUIET)
find_package(SDL2_ttf QUIET)

# SDL2_gfx doesn't have cmake config - try find_library
find_library(SDL2_GFX_LIBRARY NAMES SDL2_gfx)
find_path(SDL2_GFX_INCLUDE_DIR SDL2_gfxPrimitives.h PATH_SUFFIXES SDL2)
if(SDL2_GFX_LIBRARY AND SDL2_GFX_INCLUDE_DIR)
    set(SDL2_gfx_FOUND TRUE)
endif()

# Fallback to pkg-config if find_package didn't work
if(NOT SDL2_FOUND OR NOT SDL2_ttf_FOUND OR NOT SDL2_gfx_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        if(NOT SDL2_FOUND AND NOT TARGET SDL2::SDL2)
            pkg_check_modules(SDL2 QUIET sdl2)
        endif()
        if(NOT SDL2_ttf_FOUND AND NOT TARGET SDL2_ttf::SDL2_ttf)
            pkg_check_modules(SDL2_TTF QUIET SDL2_ttf)
        endif()
        if(NOT SDL2_gfx_FOUND)
            pkg_check_modules(SDL2_GFX QUIET SDL2_gfx)
        endif()
    endif()
endif()

# Normalize found status
set(SDL2_AVAILABLE FALSE)
if(TARGET SDL2::SDL2 OR SDL2_FOUND)
    set(SDL2_AVAILABLE TRUE)
endif()

set(SDL2_TTF_AVAILABLE FALSE)
if(TARGET SDL2_ttf::SDL2_ttf OR SDL2_ttf_FOUND OR SDL2_TTF_FOUND)
    set(SDL2_TTF_AVAILABLE TRUE)
endif()

set(SDL2_GFX_AVAILABLE FALSE)
if(SDL2_gfx_FOUND OR SDL2_GFX_FOUND)
    set(SDL2_GFX_AVAILABLE TRUE)
endif()

# Auto-enable if dependencies found
set(SDL2_ALL_FOUND FALSE)
if(SDL2_AVAILABLE AND SDL2_TTF_AVAILABLE AND SDL2_GFX_AVAILABLE)
    set(SDL2_ALL_FOUND TRUE)
endif()

if(YUI_AUTO_FIND_BACKENDS AND SDL2_ALL_FOUND)
    set(BUILD_SDL_SHOWCASE ON CACHE BOOL "Build SDL2 showcase example" FORCE)
    message(STATUS "SDL2 dependencies found - enabling SDL showcase")
endif()

if(BUILD_SDL_SHOWCASE)
    if(NOT SDL2_ALL_FOUND)
        message(FATAL_ERROR "SDL2 showcase requested but dependencies not found. Need: SDL2, SDL2_ttf, SDL2_gfx")
    endif()

    # Determine SDL2 link targets (find_package uses targets, pkg-config/find_library uses variables)
    if(TARGET SDL2::SDL2)
        set(SDL2_LINK_LIBS SDL2::SDL2)
    else()
        set(SDL2_LINK_LIBS ${SDL2_LIBRARIES})
    endif()

    # Find SDL2main for WIN32 executables (provides WinMain)
    if(NOT TARGET SDL2::SDL2main)
        find_library(SDL2_MAIN_LIBRARY NAMES SDL2main)
    endif()
    if(TARGET SDL2_ttf::SDL2_ttf)
        list(APPEND SDL2_LINK_LIBS SDL2_ttf::SDL2_ttf)
    else()
        list(APPEND SDL2_LINK_LIBS ${SDL2_TTF_LIBRARIES})
    endif()
    # SDL2_gfx: prefer find_library result, then pkg-config
    if(SDL2_GFX_LIBRARY)
        list(APPEND SDL2_LINK_LIBS ${SDL2_GFX_LIBRARY})
    else()
        list(APPEND SDL2_LINK_LIBS ${SDL2_GFX_LIBRARIES})
    endif()

    # Collect include directories
    set(SDL2_ALL_INCLUDE_DIRS)
    if(SDL2_INCLUDE_DIRS)
        list(APPEND SDL2_ALL_INCLUDE_DIRS ${SDL2_INCLUDE_DIRS})
    endif()
    if(SDL2_TTF_INCLUDE_DIRS)
        list(APPEND SDL2_ALL_INCLUDE_DIRS ${SDL2_TTF_INCLUDE_DIRS})
    endif()
    if(SDL2_GFX_INCLUDE_DIR)
        list(APPEND SDL2_ALL_INCLUDE_DIRS ${SDL2_GFX_INCLUDE_DIR})
    endif()
    if(SDL2_GFX_INCLUDE_DIRS)
        list(APPEND SDL2_ALL_INCLUDE_DIRS ${SDL2_GFX_INCLUDE_DIRS})
    endif()

    # SDL backend library
    file(GLOB SDL_BACKEND_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sdl/*.cpp
    )

    add_library(yui_sdl STATIC ${SDL_BACKEND_SOURCES})

    target_include_directories(yui_sdl
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE
            ${SDL2_ALL_INCLUDE_DIRS}
    )

    target_link_libraries(yui_sdl PUBLIC yui ${SDL2_LINK_LIBS})

    # SDL showcase executable (build with: cmake --build build --target sdl_showcase)
    add_executable(sdl_showcase WIN32 examples/sdl_showcase.cpp)

    target_include_directories(sdl_showcase PRIVATE ${SDL2_ALL_INCLUDE_DIRS})

    # Hello World minimal example (build with: cmake --build build --target hello_world)
    add_executable(hello_world WIN32 examples/hello_world.cpp)

    target_include_directories(hello_world PRIVATE ${SDL2_ALL_INCLUDE_DIRS})

    # SDL2main must be linked first for WIN32 executables (provides WinMain)
    if(TARGET SDL2::SDL2main)
        target_link_libraries(sdl_showcase PRIVATE SDL2::SDL2main)
        target_link_libraries(hello_world PRIVATE SDL2::SDL2main)
    elseif(SDL2_MAIN_LIBRARY)
        target_link_libraries(sdl_showcase PRIVATE ${SDL2_MAIN_LIBRARY})
        target_link_libraries(hello_world PRIVATE ${SDL2_MAIN_LIBRARY})
    endif()
    target_link_libraries(sdl_showcase PRIVATE yui_sdl)
    target_link_libraries(hello_world PRIVATE yui_sdl)

    # Windows: Copy DLLs to output directory
    if(WIN32)
        # Get DLL directory from CMAKE_PREFIX_PATH (MSYS2) or compiler path
        if(CMAKE_PREFIX_PATH)
            set(DLL_SEARCH_PATH "${CMAKE_PREFIX_PATH}/bin")
        else()
            get_filename_component(DLL_SEARCH_PATH ${CMAKE_CXX_COMPILER} DIRECTORY)
        endif()

        set(SDL_DLLS
            SDL2.dll SDL2_ttf.dll libSDL2_gfx-1-0-0.dll
            libfreetype-6.dll zlib1.dll libbz2-1.dll libpng16-16.dll
            libbrotlidec.dll libbrotlicommon.dll libharfbuzz-0.dll
            libglib-2.0-0.dll libintl-8.dll libiconv-2.dll
            libpcre2-8-0.dll libgraphite2.dll
            libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll
        )

        foreach(DLL ${SDL_DLLS})
            if(EXISTS "${DLL_SEARCH_PATH}/${DLL}")
                add_custom_command(TARGET sdl_showcase POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${DLL_SEARCH_PATH}/${DLL}"
                        $<TARGET_FILE_DIR:sdl_showcase>
                )
                add_custom_command(TARGET hello_world POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${DLL_SEARCH_PATH}/${DLL}"
                        $<TARGET_FILE_DIR:hello_world>
                )
            endif()
        endforeach()
    endif()
endif()

# -----------------------------------------------------------------------------
# NanoVG Showcase (optional)
# -----------------------------------------------------------------------------
# Try find_package first, then pkg-config
find_package(glfw3 QUIET)
find_package(GLEW QUIET)

if(NOT glfw3_FOUND OR NOT GLEW_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        if(NOT glfw3_FOUND)
            pkg_check_modules(GLFW QUIET glfw3)
        endif()
        if(NOT GLEW_FOUND)
            pkg_check_modules(GLEW_PKG QUIET glew)
        endif()
    endif()
endif()

# Normalize found status
if(TARGET glfw)
    set(GLFW_AVAILABLE TRUE)
elseif(GLFW_FOUND)
    set(GLFW_AVAILABLE TRUE)
else()
    set(GLFW_AVAILABLE FALSE)
endif()

if(TARGET GLEW::GLEW)
    set(GLEW_AVAILABLE TRUE)
elseif(GLEW_PKG_FOUND)
    set(GLEW_AVAILABLE TRUE)
else()
    set(GLEW_AVAILABLE FALSE)
endif()

# NanoVG is bundled as a submodule
set(NANOVG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/nanovg)

# Auto-enable if dependencies found (nanovg is always available via submodule)
if(YUI_AUTO_FIND_BACKENDS AND GLFW_AVAILABLE AND GLEW_AVAILABLE AND EXISTS "${NANOVG_DIR}/src/nanovg.c")
    set(BUILD_NVG_SHOWCASE ON CACHE BOOL "Build NanoVG showcase example" FORCE)
    message(STATUS "NanoVG dependencies found - enabling NVG showcase")
endif()

if(BUILD_NVG_SHOWCASE)
    if(NOT GLFW_AVAILABLE)
        message(FATAL_ERROR "NanoVG showcase requested but GLFW3 not found")
    endif()
    if(NOT GLEW_AVAILABLE)
        message(FATAL_ERROR "NanoVG showcase requested but GLEW not found")
    endif()
    if(NOT EXISTS "${NANOVG_DIR}/src/nanovg.c")
        message(FATAL_ERROR "NanoVG submodule not found. Run: git submodule update --init")
    endif()

    # Determine link targets
    if(TARGET glfw)
        set(GLFW_LINK_LIBS glfw)
    else()
        set(GLFW_LINK_LIBS ${GLFW_LIBRARIES})
    endif()
    if(TARGET GLEW::GLEW)
        set(GLEW_LINK_LIBS GLEW::GLEW)
    else()
        set(GLEW_LINK_LIBS ${GLEW_PKG_LIBRARIES})
    endif()

    # Build nanovg from submodule
    add_library(nanovg STATIC ${NANOVG_DIR}/src/nanovg.c)
    target_include_directories(nanovg PUBLIC ${NANOVG_DIR}/src)

    # Find OpenGL
    find_package(OpenGL REQUIRED)

    # NVG backend library
    file(GLOB NVG_BACKEND_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/nvg/*.cpp
    )

    add_library(yui_nvg STATIC ${NVG_BACKEND_SOURCES})

    target_include_directories(yui_nvg
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE
            ${GLFW_INCLUDE_DIRS}
            ${GLEW_INCLUDE_DIRS}
            ${GLEW_PKG_INCLUDE_DIRS}
    )

    target_link_libraries(yui_nvg PUBLIC yui nanovg ${GLFW_LINK_LIBS} ${GLEW_LINK_LIBS} OpenGL::GL)

    # NVG showcase executable (build with: cmake --build build --target nvg_showcase)
    add_executable(nvg_showcase WIN32 examples/nvg_showcase.cpp)

    target_link_libraries(nvg_showcase PRIVATE yui_nvg)

    # Todo app example (build with: cmake --build build --target todo_app)
    add_executable(todo_app WIN32 examples/todo_app.cpp)

    target_link_libraries(todo_app PRIVATE yui_nvg)

    # Pong game example (build with: cmake --build build --target pong)
    add_executable(pong WIN32 examples/pong.cpp)

    target_link_libraries(pong PRIVATE yui_nvg)

    # Benchmark example (build with: cmake --build build --target benchmark)
    add_executable(benchmark WIN32 examples/benchmark.cpp)

    target_link_libraries(benchmark PRIVATE yui_nvg)

    # Cascading menu example (build with: cmake --build build --target cascading_menu)
    add_executable(cascading_menu WIN32 examples/cascading_menu.cpp)

    target_link_libraries(cascading_menu PRIVATE yui_nvg)

    # Context menu example (build with: cmake --build build --target context_menu)
    add_executable(context_menu WIN32 examples/context_menu.cpp)

    target_link_libraries(context_menu PRIVATE yui_nvg)

    # Windows: Copy DLLs to output directory
    if(WIN32)
        if(CMAKE_PREFIX_PATH)
            set(DLL_SEARCH_PATH "${CMAKE_PREFIX_PATH}/bin")
        else()
            get_filename_component(DLL_SEARCH_PATH ${CMAKE_CXX_COMPILER} DIRECTORY)
        endif()

        set(NVG_DLLS
            glfw3.dll glew32.dll
            libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll
        )

        foreach(DLL ${NVG_DLLS})
            if(EXISTS "${DLL_SEARCH_PATH}/${DLL}")
                add_custom_command(TARGET nvg_showcase POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${DLL_SEARCH_PATH}/${DLL}"
                        $<TARGET_FILE_DIR:nvg_showcase>
                )
                add_custom_command(TARGET todo_app POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${DLL_SEARCH_PATH}/${DLL}"
                        $<TARGET_FILE_DIR:todo_app>
                )
                add_custom_command(TARGET pong POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${DLL_SEARCH_PATH}/${DLL}"
                        $<TARGET_FILE_DIR:pong>
                )
                add_custom_command(TARGET benchmark POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${DLL_SEARCH_PATH}/${DLL}"
                        $<TARGET_FILE_DIR:benchmark>
                )
                add_custom_command(TARGET cascading_menu POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${DLL_SEARCH_PATH}/${DLL}"
                        $<TARGET_FILE_DIR:cascading_menu>
                )
                add_custom_command(TARGET context_menu POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${DLL_SEARCH_PATH}/${DLL}"
                        $<TARGET_FILE_DIR:context_menu>
                )
            endif()
        endforeach()
    endif()
endif()

# -----------------------------------------------------------------------------
# "all_examples" target - builds all example executables
# -----------------------------------------------------------------------------
set(ALL_EXAMPLE_TARGETS)
if(BUILD_SDL_SHOWCASE)
    list(APPEND ALL_EXAMPLE_TARGETS sdl_showcase hello_world)
endif()
if(BUILD_NVG_SHOWCASE)
    list(APPEND ALL_EXAMPLE_TARGETS nvg_showcase todo_app pong benchmark cascading_menu context_menu)
endif()

if(ALL_EXAMPLE_TARGETS)
    add_custom_target(all_examples
        DEPENDS ${ALL_EXAMPLE_TARGETS}
        COMMENT "Building all examples"
    )
endif()

# -----------------------------------------------------------------------------
# Code quality targets (format, tidy)
# -----------------------------------------------------------------------------
# All files for formatting (includes headers)
file(GLOB_RECURSE FORMAT_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.hpp
)

find_program(CLANG_FORMAT clang-format HINTS "C:/Program Files/LLVM/bin")
if(CLANG_FORMAT)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${FORMAT_SOURCE_FILES}
        COMMENT "Running clang-format on all source files"
        VERBATIM
    )
    # Check-only version for CI
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --dry-run --Werror ${FORMAT_SOURCE_FILES}
        COMMENT "Checking clang-format compliance"
        VERBATIM
    )
endif()


# -----------------------------------------------------------------------------
# Install configuration
# -----------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS yui yogacore
    EXPORT yuiTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/yui
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    # Internal headers (the reconciliation algorithm, the fiber-tree machinery,
    # the backend RAII scope helpers) live under any "detail/" subdirectory and
    # are deliberately NOT installed — they must not become part of the frozen
    # 1.0 public ABI. This excludes include/yui/detail, include/yui/nvg/detail,
    # and include/yui/sdl/detail.
    PATTERN "detail" EXCLUDE
    # version.hpp.in is a configure_file template (@PROJECT_VERSION@ placeholders);
    # ship only the generated version.hpp (installed below), never the template.
    PATTERN "*.in" EXCLUDE
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/yui/version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/yui
)

install(EXPORT yuiTargets
    FILE yuiTargets.cmake
    NAMESPACE yui::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/yui
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/yui-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/yui-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/yui
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/yui-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/yui-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/yui-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/yui
)

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------
message(STATUS "")
message(STATUS "yui ${PROJECT_VERSION} configuration:")
message(STATUS "  C++ Standard:    ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build tests:     ${BUILD_TESTING}")
message(STATUS "  SDL showcase:    ${BUILD_SDL_SHOWCASE}")
message(STATUS "  NVG showcase:    ${BUILD_NVG_SHOWCASE}")
if(CLANG_FORMAT)
    message(STATUS "  clang-format:    ${CLANG_FORMAT}")
endif()
message(STATUS "")