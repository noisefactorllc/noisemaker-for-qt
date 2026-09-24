# examples/quick — NoisemakerItem in a Qt Quick scene (qt/quick/noisemaker_item.h).
# Configure it like examples/viewer: against an installed package
#
#   cmake -B build -S examples/quick -DCMAKE_PREFIX_PATH="<Qt>;<noisemaker-qt prefix>"
#
# (the package must have been built with NM_QT_BUILD_QUICK, the default when
# Qt Quick is installed), or, with no installed package on the prefix path,
# against the sibling qt/ tree.
cmake_minimum_required(VERSION 3.21)
project(noisemaker-qt-quick-example CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core Gui Quick REQUIRED)

find_package(noisemaker-qt 0.1 CONFIG QUIET COMPONENTS Quick)
if(noisemaker-qt_FOUND)
    message(STATUS "examples/quick: using installed noisemaker-qt package (data root: ${NOISEMAKER_QT_DATA_ROOT})")
    set(NM_EXAMPLE_DATA_ROOT ${NOISEMAKER_QT_DATA_ROOT})
else()
    message(STATUS "examples/quick: no installed noisemaker-qt package with the Quick component -- building the dev tree (add_subdirectory ../../qt)")
    set(NM_QT_BUILD_QUICK ON CACHE BOOL "Build the Qt Quick item" FORCE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../qt ${CMAKE_CURRENT_BINARY_DIR}/_noisemaker-qt)
    set(NM_EXAMPLE_DATA_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../qt/noisemaker)
endif()

add_executable(quick main.cpp)
qt_add_resources(quick "qml" PREFIX "/" FILES main.qml)
target_link_libraries(quick PRIVATE noisemaker-qt::quick Qt6::Quick)
# Baked default data root; the NOISEMAKER_QT_DATA_ROOT environment variable
# overrides it at run time.
target_compile_definitions(quick PRIVATE NM_DATA_ROOT="${NM_EXAMPLE_DATA_ROOT}")
