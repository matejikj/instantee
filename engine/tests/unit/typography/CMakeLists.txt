add_executable(spike_typography_shape_test text_shape_test.cpp)
target_link_libraries(spike_typography_shape_test PRIVATE
  instantee::typography instantee_build_options)
target_compile_definitions(spike_typography_shape_test PRIVATE
  "INSTANTEE_PINNED_FONT_DIR=\"${PROJECT_SOURCE_DIR}/third_party/fonts\"")
add_test(NAME spike_typography_shape COMMAND spike_typography_shape_test)
set_tests_properties(spike_typography_shape PROPERTIES TIMEOUT 60 LABELS
  "cpu;smoke")
