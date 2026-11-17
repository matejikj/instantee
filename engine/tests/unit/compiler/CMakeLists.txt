add_executable(spike_compiler_identity_test identity_test.cpp)
target_link_libraries(spike_compiler_identity_test PRIVATE instantee::compiler
  instantee_build_options)
target_compile_definitions(spike_compiler_identity_test PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_compiler_identity COMMAND spike_compiler_identity_test)
set_tests_properties(spike_compiler_identity PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_compiler_cds_compile_test cds_compile_test.cpp)
target_link_libraries(spike_compiler_cds_compile_test PRIVATE
  instantee::compiler instantee_build_options)
target_compile_definitions(spike_compiler_cds_compile_test PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_compiler_cds_compile COMMAND spike_compiler_cds_compile_test)
set_tests_properties(spike_compiler_cds_compile PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_compiler_track_conflict_test track_conflict_test.cpp)
target_link_libraries(spike_compiler_track_conflict_test PRIVATE
  instantee::compiler instantee_build_options)
add_test(NAME spike_compiler_track_conflict
  COMMAND spike_compiler_track_conflict_test)
set_tests_properties(spike_compiler_track_conflict PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_compiler_runtime_scene_test runtime_scene_test.cpp)
target_link_libraries(spike_compiler_runtime_scene_test PRIVATE
  instantee::compiler instantee_build_options)
add_test(NAME spike_compiler_runtime_scene
  COMMAND spike_compiler_runtime_scene_test)
set_tests_properties(spike_compiler_runtime_scene PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_compiler_properties_validate_test
  properties_validate_test.cpp)
target_link_libraries(spike_compiler_properties_validate_test PRIVATE
  instantee::compiler instantee_build_options)
add_test(NAME spike_compiler_properties_validate
  COMMAND spike_compiler_properties_validate_test)
set_tests_properties(spike_compiler_properties_validate PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_compiler_transition_compile_test
  transition_compile_test.cpp)
target_link_libraries(spike_compiler_transition_compile_test PRIVATE
  instantee::compiler instantee::runtime instantee_build_options)
add_test(NAME spike_compiler_transition_compile
  COMMAND spike_compiler_transition_compile_test)
set_tests_properties(spike_compiler_transition_compile PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")
