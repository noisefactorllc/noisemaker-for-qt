# examples/viewer — a minimal live QOpenGLWidget embedding of nm::Backend
# (ARCHITECTURE.md "Integration surface"). A standalone CMake project (its
# own `project()`, its own build dir) so it can be configured exactly the
# way a real downstream consumer would: either against an INSTALLED
# noisemaker-qt package, or — if none is on CMAKE_PREFIX_PATH — against the
# sibling qt/ dev tree directly. Widgets/OpenGLWidgets are a dependency of
# THIS example only; libnoisemaker-qt itself stays Core/Gui/OpenGL-only
# (ARCHITECTURE.md).
cmake_minimum_required(VERSION 3.20)
project(noisemaker-qt-viewer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core Gui OpenGL Widgets OpenGLWidgets REQUIRED)

# Installed-package flow (find_package) vs dev-tree flow (add_subdirectory).
# Both are real, both are exercised (see task-T7-report.md):
#
#   installed:  cmake -B examples/viewer/build -S examples/viewer \
#                 -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt;<scratch-install-prefix>"
#   dev tree:   cmake -B examples/viewer/build -S examples/viewer \
#                 -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
#
# The second form is the brief's own literal quickstart command — with no
# noisemaker-qt package on the prefix path, it falls through to
# add_subdirectory below.
find_package(noisemaker-qt CONFIG QUIET)
if(noisemaker-qt_FOUND)
    message(STATUS "examples/viewer: using installed noisemaker-qt package (data root: ${NOISEMAKER_QT_DATA_ROOT})")
    set(NM_VIEWER_LIB noisemaker-qt::noisemaker-qt)
    set(NM_VIEWER_DATA_ROOT ${NOISEMAKER_QT_DATA_ROOT})
else()
    message(STATUS "examples/viewer: no installed noisemaker-qt package found -- building the dev tree (add_subdirectory ../../qt)")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../qt ${CMAKE_CURRENT_BINARY_DIR}/_noisemaker-qt)
    set(NM_VIEWER_LIB noisemaker-qt)
    set(NM_VIEWER_DATA_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../qt/noisemaker)
endif()

add_executable(viewer main.cpp viewer.h viewer.cpp)
target_link_libraries(viewer PRIVATE ${NM_VIEWER_LIB} Qt6::Widgets Qt6::OpenGLWidgets)

# Baked default dataRoot (see viewer.cpp resolveDataRoot(); NM_DATA_ROOT
# environment variable overrides this at runtime).
target_compile_definitions(viewer PRIVATE NM_DATA_ROOT="${NM_VIEWER_DATA_ROOT}")
