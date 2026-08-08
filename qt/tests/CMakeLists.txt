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
