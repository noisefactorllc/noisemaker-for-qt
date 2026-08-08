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
