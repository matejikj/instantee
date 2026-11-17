add_executable(spike_compiler_identity_test identity_test.cpp)
target_link_libraries(spike_compiler_identity_test PRIVATE instantee::compiler
  instantee_build_options)
target_compile_definitions(spike_compiler_identity_test PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_compiler_identity COMMAND spike_compiler_identity_test)
set_tests_properties(spike_compiler_identity PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")
