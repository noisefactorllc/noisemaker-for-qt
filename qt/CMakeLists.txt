# Noisemaker for Qt: static library, nm-render CLI, and unit tests.
#
# Embedding: a host may add this directory with add_subdirectory() or
# FetchContent. Tools, tests and install rules then default OFF, so the
# host's build and ctest contain only the library. Set the options below to
# override. The host locates the runtime data tree through
# NOISEMAKER_QT_DATA_ROOT (also exported as NOISEMAKER_QT_DATA_DIR and as the
# NOISEMAKER_QT_DATA_ROOT property of the noisemaker-qt target). Link the
# library as noisemaker-qt::noisemaker-qt, the same name the installed
# package exports.
cmake_minimum_required(VERSION 3.21)
# The version is the port's release series; export-kit/kit.config.json
# "series" states the same series for the export kit releases
# (kit-qt-v<series>.<n>), checked below.
project(noisemaker-qt VERSION 0.1 LANGUAGES CXX)

option(NM_QT_BUILD_TOOLS "Build the nm-render CLI" ${PROJECT_IS_TOP_LEVEL})
option(NM_QT_BUILD_TESTS "Build the unit tests and register them with ctest" ${PROJECT_IS_TOP_LEVEL})
option(NM_QT_INSTALL "Generate install rules and the package config" ${PROJECT_IS_TOP_LEVEL})

set(_nm_kit_config "${CMAKE_CURRENT_SOURCE_DIR}/../export-kit/kit.config.json")
if(EXISTS "${_nm_kit_config}")
    file(READ "${_nm_kit_config}" _nm_kit_json)
    string(JSON _nm_kit_series GET "${_nm_kit_json}" series)
    if(NOT _nm_kit_series STREQUAL PROJECT_VERSION)
        message(FATAL_ERROR "project VERSION ${PROJECT_VERSION} differs from export-kit/kit.config.json series ${_nm_kit_series}")
    endif()
endif()

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
target_compile_features(noisemaker-qt PUBLIC cxx_std_17)
add_library(noisemaker-qt::noisemaker-qt ALIAS noisemaker-qt)
# CPU-side arithmetic mirrors the JavaScript reference, which never fuses a
# multiply and add. Clang contracts `a + b * c` into one FMA by default on
# arm64 (measured: midi() CC 64 -> rotation 1.4173228346456668 instead of
# the reference 1.4173228346456597), and GCC does so with FMA-capable
# targets. Explicit std::fma (audio_analyzer.cpp, arm64 Chromium down-mix)
# is unaffected.
target_compile_options(noisemaker-qt PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffp-contract=off>)

# Runtime data tree (shaders/, effects/ and share/meshes/) for
# nm::Backend::setup() and nm::EffectRegistry::loadAll(). Visible to a
# parent project through the cache and through a target property.
set(NOISEMAKER_QT_DATA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/noisemaker"
    CACHE INTERNAL "noisemaker-qt runtime data root (shaders/, effects/, share/)")
set(NOISEMAKER_QT_DATA_DIR "${NOISEMAKER_QT_DATA_ROOT}"
    CACHE INTERNAL "Alias of NOISEMAKER_QT_DATA_ROOT")
set_target_properties(noisemaker-qt PROPERTIES
    NOISEMAKER_QT_DATA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/noisemaker")

if(NM_QT_BUILD_TOOLS)
    # nm-render CLI sources are GLOB'd (not listed explicitly) so that later
    # tasks can drop in new translation units — dump_*.cpp (compiler track:
    # T8/T9/T10) and any render_*.cpp (renderer track: T5's --samples) — that
    # register into main.cpp's pre-declared flag hooks (flag_hooks.h) without
    # this file ever being edited again. CONFIGURE_DEPENDS re-globs on
    # reconfigure/build with generators that support it (Ninja, Makefiles).
    file(GLOB NM_RENDER_CLI_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/nm-render/*.cpp
    )
    add_executable(nm-render ${NM_RENDER_CLI_SOURCES})
    set_target_properties(nm-render PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(nm-render PRIVATE noisemaker-qt Qt6::Core Qt6::Gui Qt6::OpenGL)
endif()

if(NM_QT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------- install / find_package
# Makes libnoisemaker-qt consumable via find_package(noisemaker-qt CONFIG).
# A hand-rolled IMPORTED-target config defines the target against the
# installed layout (cmake/noisemaker-qt-config.cmake.in).
if(NM_QT_INSTALL)
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
    # filter/text's default family (nm::registerBundledFonts), with its OFL.
    install(DIRECTORY noisemaker/fonts DESTINATION ${CMAKE_INSTALL_DATADIR}/noisemaker-qt/noisemaker)
    # Built-in OBJ meshes (effect definition builtinMeshes paths, relative to
    # the data root) that hosts load for render/meshLoader.
    install(DIRECTORY noisemaker/share DESTINATION ${CMAKE_INSTALL_DATADIR}/noisemaker-qt/noisemaker)

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/noisemaker-qt-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/noisemaker-qt-config.cmake
        @ONLY
    )
    # 0.x series: a new minor version may break the API.
    include(CMakePackageConfigHelpers)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/noisemaker-qt-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion
    )
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/noisemaker-qt-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/noisemaker-qt-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/noisemaker-qt
    )
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()
