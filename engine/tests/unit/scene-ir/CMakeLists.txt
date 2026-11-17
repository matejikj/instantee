add_executable(spike_scene_ir_load_test load_test.cpp)
target_link_libraries(spike_scene_ir_load_test PRIVATE instantee::scene_ir
  instantee_build_options)
target_compile_definitions(spike_scene_ir_load_test PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_scene_ir_load COMMAND spike_scene_ir_load_test)
set_tests_properties(spike_scene_ir_load PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_scene_ir_graph_test graph_test.cpp)
target_link_libraries(spike_scene_ir_graph_test PRIVATE instantee::scene_ir
  instantee_build_options)
target_compile_definitions(spike_scene_ir_graph_test PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_scene_ir_graph COMMAND spike_scene_ir_graph_test)
set_tests_properties(spike_scene_ir_graph PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_scene_ir_security_test security_test.cpp)
target_link_libraries(spike_scene_ir_security_test PRIVATE instantee::scene_ir
  instantee_build_options)
target_compile_definitions(spike_scene_ir_security_test PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_scene_ir_security COMMAND spike_scene_ir_security_test)
set_tests_properties(spike_scene_ir_security PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_scene_ir_canonicalize_test canonicalize_test.cpp)
target_link_libraries(spike_scene_ir_canonicalize_test PRIVATE
  instantee::scene_ir instantee_build_options)
target_compile_definitions(spike_scene_ir_canonicalize_test PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_scene_ir_canonicalize COMMAND spike_scene_ir_canonicalize_test)
set_tests_properties(spike_scene_ir_canonicalize PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_scene_ir_lockfile_test lockfile_test.cpp)
target_link_libraries(spike_scene_ir_lockfile_test PRIVATE instantee::scene_ir
  instantee_build_options)
target_compile_definitions(spike_scene_ir_lockfile_test PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_scene_ir_lockfile COMMAND spike_scene_ir_lockfile_test)
set_tests_properties(spike_scene_ir_lockfile PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")
