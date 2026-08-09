# Noisemaker for Qt — CMake skeleton.
#
# This file and qt/tools/nm-render/main.cpp are created ONCE here (T3) with
# both tracks' hook points pre-declared (see qt/tools/nm-render/flag_hooks.h
# and main.cpp). Every later task edits only its own sources.cmake / its own
# qt/tools/nm-render/*.cpp translation units — never this file. If a change
# here is ever unavoidable, stop and report instead of editing (T3 task
# brief, "File ownership rules").
cmake_minimum_required(VERSION 3.20)
project(noisemaker-qt CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core Gui OpenGL REQUIRED)

include(${CMAKE_CURRENT_SOURCE_DIR}/noisemaker/runtime/sources.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/noisemaker/compiler/sources.cmake)

add_library(noisemaker-qt STATIC
    ${NM_RUNTIME_SOURCES}
    ${NM_COMPILER_SOURCES}
)
target_include_directories(noisemaker-qt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/noisemaker
)
target_link_libraries(noisemaker-qt PUBLIC Qt6::Core Qt6::Gui Qt6::OpenGL)

# nm-render CLI sources are GLOB'd (not listed explicitly) so that later
# tasks can drop in new translation units — dump_*.cpp (compiler track:
# T8/T9/T10) and any render_*.cpp (renderer track: T5's --samples) — that
# register into main.cpp's pre-declared flag hooks (flag_hooks.h) without
# this file ever being edited again. CONFIGURE_DEPENDS re-globs on
# reconfigure/build with generators that support it (Ninja here).
file(GLOB NM_RENDER_CLI_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/nm-render/*.cpp
)
add_executable(nm-render ${NM_RENDER_CLI_SOURCES})
set_target_properties(nm-render PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_libraries(nm-render PRIVATE noisemaker-qt Qt6::Core Qt6::Gui Qt6::OpenGL)

enable_testing()
add_subdirectory(tests)

# ---------------------------------------------------------------- install / find_package (T7)
# T7 (task-T7-brief.md) is explicitly authorized to add install/export rules
# to this file ("the renderer track is done and the compiler track never
# touches this file, so the edit is conflict-free") — the ONLY lines below
# this point are new; nothing above is touched. Makes `libnoisemaker-qt`
# consumable via `find_package(noisemaker-qt CONFIG)`, the path
# examples/viewer's CMakeLists.txt uses when no add_subdirectory dev tree is
# available.
#
# Deliberately a hand-rolled IMPORTED-target Config rather than
# `install(EXPORT ...)`: the latter would force the existing
# target_include_directories() call above (line 25) to grow
# BUILD_INTERFACE/INSTALL_INTERFACE generator-expression wrapping just to
# export cleanly — a change to a pre-existing, "never edited again" line for
# a benefit this approach gets without it. One Config file that defines the
# imported target directly against the INSTALLED layout is also more nearly
# "minimal" per the brief's own wording.
include(GNUInstallDirs)

install(TARGETS noisemaker-qt
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Public headers: mirrors the two PUBLIC include roots set above (qt/ and
# qt/noisemaker/) so both `#include "noisemaker/runtime/backend.h"` and
# `#include "runtime/backend.h"` keep working for an installed consumer,
# exactly as they do in-tree (see noisemaker-qt-config.cmake.in). compiler/
# and runtime/ installed separately (rather than the whole noisemaker/ tree
# in one call) so FILES_MATCHING's directory-mirroring doesn't also create
# hundreds of empty directories for the sibling shaders/effects subtrees,
# which contain no headers at all.
install(DIRECTORY noisemaker/compiler/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/noisemaker-qt/noisemaker/compiler
    FILES_MATCHING PATTERN "*.h"
)
install(DIRECTORY noisemaker/runtime/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/noisemaker-qt/noisemaker/runtime
    FILES_MATCHING PATTERN "*.h"
)

# Runtime shader/effect data: nm::Backend / nm::EffectRegistry load these
# from a filesystem `dataRoot` at RUNTIME, never compiled in (see
# docs/GRAPH-JSON-SCHEMA.md "Qt consumer", PORTING-GUIDE.md). Installed
# alongside the lib so an installed-package consumer can actually render,
# not merely link.
install(DIRECTORY noisemaker/shaders DESTINATION ${CMAKE_INSTALL_DATADIR}/noisemaker-qt/noisemaker)
install(DIRECTORY noisemaker/effects DESTINATION ${CMAKE_INSTALL_DATADIR}/noisemaker-qt/noisemaker)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/noisemaker-qt-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/noisemaker-qt-config.cmake
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/noisemaker-qt-config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/noisemaker-qt
)
