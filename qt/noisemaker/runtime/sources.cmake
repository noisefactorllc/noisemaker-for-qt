# Renderer track (T3-T6) owns this file. Explicit list (not a glob) so
# additions are a deliberate, reviewable edit — T5/T6 append to this list
# (e.g. agents.{h,cpp}, pingpong.{h,cpp}) without touching qt/CMakeLists.txt.
# Paths are CMAKE_CURRENT_LIST_DIR-relative (this file's own directory), not
# CMAKE_CURRENT_SOURCE_DIR-relative, so they resolve correctly regardless of
# where the top-level CMakeLists.txt include()s this file from.
set(NM_RUNTIME_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/graph.h
    ${CMAKE_CURRENT_LIST_DIR}/graph.cpp
    ${CMAKE_CURRENT_LIST_DIR}/shader_assembly.h
    ${CMAKE_CURRENT_LIST_DIR}/shader_assembly.cpp
    ${CMAKE_CURRENT_LIST_DIR}/surface.h
    ${CMAKE_CURRENT_LIST_DIR}/surface.cpp
    ${CMAKE_CURRENT_LIST_DIR}/pingpong.h
    ${CMAKE_CURRENT_LIST_DIR}/pingpong.cpp
    ${CMAKE_CURRENT_LIST_DIR}/agents.h
    ${CMAKE_CURRENT_LIST_DIR}/agents.cpp
    ${CMAKE_CURRENT_LIST_DIR}/output_sink.h
    ${CMAKE_CURRENT_LIST_DIR}/output_sink.cpp
    ${CMAKE_CURRENT_LIST_DIR}/frame_export.h
    ${CMAKE_CURRENT_LIST_DIR}/frame_export.cpp
    ${CMAKE_CURRENT_LIST_DIR}/backend.h
    ${CMAKE_CURRENT_LIST_DIR}/backend.cpp
    ${CMAKE_CURRENT_LIST_DIR}/png_io.h
    ${CMAKE_CURRENT_LIST_DIR}/png_io.cpp
)
