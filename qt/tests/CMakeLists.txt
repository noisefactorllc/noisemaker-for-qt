# Plain executables, no test framework dependency (T3 brief Step 2:
# "QtTest or plain `assert` main -- keep it dependency-free"). Each links
# the noisemaker-qt static library and registers as its own ctest case, so
# `ctest --test-dir qt/build` reports both by name.
add_executable(test_shader_assembly test_shader_assembly.cpp)
target_link_libraries(test_shader_assembly PRIVATE noisemaker-qt Qt6::Core)
add_test(NAME test_shader_assembly COMMAND test_shader_assembly)

add_executable(test_graph_load test_graph_load.cpp)
target_link_libraries(test_graph_load PRIVATE noisemaker-qt Qt6::Core)
add_test(NAME test_graph_load COMMAND test_graph_load)

add_executable(test_output_runtime test_output_runtime.cpp)
target_link_libraries(test_output_runtime PRIVATE noisemaker-qt Qt6::Core)
add_test(NAME test_output_runtime COMMAND test_output_runtime)

add_executable(test_frame_export test_frame_export.cpp)
target_link_libraries(test_frame_export PRIVATE noisemaker-qt Qt6::Core Qt6::Gui Qt6::OpenGL)
add_test(NAME test_frame_export COMMAND test_frame_export)
set_tests_properties(test_frame_export PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)

# Compiler track (T8-T10) tests, same plain-executable convention.
add_executable(test_lexer test_lexer.cpp)
target_link_libraries(test_lexer PRIVATE noisemaker-qt Qt6::Core)
add_test(NAME test_lexer COMMAND test_lexer)

add_executable(test_parser test_parser.cpp)
target_link_libraries(test_parser PRIVATE noisemaker-qt Qt6::Core)
add_test(NAME test_parser COMMAND test_parser)

# T9 (registry + validator), same plain-executable convention. Unlike
# test_lexer/test_parser (pure in-memory string -> AST, no file I/O), these
# two load the real qt/noisemaker/effects/**.json catalog via
# EffectRegistry::defaultDataRoot() (CWD-relative "qt/noisemaker" --
# effect_registry.h). ctest's default working directory is the test's own
# build-tree directory (qt/build-compiler/tests), NOT the repo root, so
# without WORKING_DIRECTORY here defaultDataRoot() silently resolves to a
# nonexistent path and both tests fail every check (verified: caught by a
# from-scratch `ctest --test-dir qt/build-compiler` run, which failed both
# even though running the binaries directly from the repo root passed
# every check -- see task report). Pin it to the repo root explicitly
# rather than teach EffectRegistry an executable-relative fallback (main.
# cpp's resolveDataRoot() equivalent) that would depend on the exact
# build-tree depth of whichever binary calls it.
add_executable(test_registry test_registry.cpp)
target_link_libraries(test_registry PRIVATE noisemaker-qt Qt6::Core)
add_test(NAME test_registry COMMAND test_registry)
set_tests_properties(test_registry PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)

add_executable(test_validator test_validator.cpp)
target_link_libraries(test_validator PRIVATE noisemaker-qt Qt6::Core)
add_test(NAME test_validator COMMAND test_validator)
set_tests_properties(test_validator PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)

# Parity-gate helper (NOT a ctest case -- like dump_lex.cpp/dump_parse.cpp,
# this is CLI plumbing consumed by parity/check_registry.mjs, not a
# pass/fail assertion binary). Prints EffectRegistry::dumpSummary() as
# JSON for the given dataRoot (argv[1], default EffectRegistry::
# defaultDataRoot()). Lives here rather than in qt/tools/nm-render/ because
# check_registry.mjs must go green in Step 1, before the validator (and
# its dump_validate.cpp candidate binary) exist -- see task report.
add_executable(registry_dump registry_dump.cpp)
target_link_libraries(registry_dump PRIVATE noisemaker-qt Qt6::Core)

# T10 (expander): same plain-executable convention.
add_executable(test_expander test_expander.cpp)
target_link_libraries(test_expander PRIVATE noisemaker-qt Qt6::Core)
add_test(NAME test_expander COMMAND test_expander)
set_tests_properties(test_expander PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)

add_executable(test_resources test_resources.cpp)
target_link_libraries(test_resources PRIVATE noisemaker-qt Qt6::Core)
add_test(NAME test_resources COMMAND test_resources)

# Parity-gate helper (NOT a ctest case) -- candidate side of
# parity/check_expand.mjs. See expand_dump.cpp header for why this is a
# standalone binary rather than an nm-render flag (no --dump-expand slot
# was pre-declared in flag_hooks.h at T3; mirrors registry_dump.cpp's own
# precedent exactly).
add_executable(expand_dump expand_dump.cpp)
target_link_libraries(expand_dump PRIVATE noisemaker-qt Qt6::Core)
