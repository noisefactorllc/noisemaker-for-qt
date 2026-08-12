cmake_minimum_required(VERSION 3.20)
project(noisemaker-qt-export LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Core Gui OpenGL REQUIRED)

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

add_executable(noisemaker-qt-export src/main.cpp)
target_link_libraries(noisemaker-qt-export PRIVATE noisemaker-qt)
target_compile_definitions(noisemaker-qt-export PRIVATE
    NM_DATA_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    NM_PROGRAM_PATH="${CMAKE_CURRENT_SOURCE_DIR}/program.dsl"
)
