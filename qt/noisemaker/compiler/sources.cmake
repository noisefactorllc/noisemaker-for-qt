# Compiler track (T8-T10) owns this file. T8 populates lexer/parser;
# T9/T10 will append effect_registry/enums/validator/expander/etc.
# Feeds into the noisemaker-qt static library target alongside
# NM_RUNTIME_SOURCES; see qt/CMakeLists.txt.
#
# tokens.h and ast.h are header-only (constants + trivial QJsonObject
# builders) so they have no corresponding .cpp translation unit.
set(NM_COMPILER_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/diagnostics.cpp
    ${CMAKE_CURRENT_LIST_DIR}/lexer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/parser.cpp
)
