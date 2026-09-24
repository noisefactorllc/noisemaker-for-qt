cmake_minimum_required(VERSION 3.21)
project(noisemaker-qt-export LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# A renderer: build optimized unless the caller chose a configuration.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 COMPONENTS Core Gui OpenGL REQUIRED)
if(Qt6_VERSION VERSION_LESS 6.9)
    message(WARNING "Qt ${Qt6_VERSION} cannot set a variable font's weight axis (added in Qt 6.9): "
                    "filter/text draws the bundled Nunito at its default ExtraLight weight. "
                    "Qt 6.9 or later is supported.")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/engine/noisemaker/runtime/sources.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/engine/noisemaker/compiler/sources.cmake)

add_library(noisemaker-qt STATIC
    ${NM_RUNTIME_SOURCES}
    ${NM_COMPILER_SOURCES}
)
target_include_directories(noisemaker-qt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/noisemaker
)
target_link_libraries(noisemaker-qt PUBLIC Qt6::Core Qt6::Gui Qt6::OpenGL)
# The same flag as the port's own build: the JavaScript reference never
# fuses a multiply and add, so automation values must not either.
target_compile_options(noisemaker-qt PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffp-contract=off>)

add_executable(noisemaker-qt-export src/main.cpp src/host_inputs.h src/host_inputs.cpp)
target_link_libraries(noisemaker-qt-export PRIVATE noisemaker-qt)
target_compile_definitions(noisemaker-qt-export PRIVATE
    NM_DATA_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    NM_PROGRAM_PATH="${CMAKE_CURRENT_SOURCE_DIR}/program.dsl"
)
