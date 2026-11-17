add_executable(spike_typography_shape_test text_shape_test.cpp)
target_link_libraries(spike_typography_shape_test PRIVATE
  instantee::typography instantee_build_options)
target_compile_definitions(spike_typography_shape_test PRIVATE
  "INSTANTEE_PINNED_FONT_DIR=\"${PROJECT_SOURCE_DIR}/third_party/fonts\"")
add_test(NAME spike_typography_shape COMMAND spike_typography_shape_test)
set_tests_properties(spike_typography_shape PROPERTIES TIMEOUT 60 LABELS
  "cpu;smoke")

add_executable(spike_typography_glyph_atlas_test glyph_atlas_test.cpp)
target_link_libraries(spike_typography_glyph_atlas_test PRIVATE
  instantee::typography instantee_build_options)
target_compile_definitions(spike_typography_glyph_atlas_test PRIVATE
  "INSTANTEE_PINNED_FONT_DIR=\"${PROJECT_SOURCE_DIR}/third_party/fonts\"")
add_test(NAME spike_typography_glyph_atlas
         COMMAND spike_typography_glyph_atlas_test)
set_tests_properties(spike_typography_glyph_atlas PROPERTIES TIMEOUT 60 LABELS
  "cpu;smoke")

add_executable(spike_typography_kinetic_layout_test kinetic_layout_test.cpp)
target_link_libraries(spike_typography_kinetic_layout_test PRIVATE
  instantee::typography instantee_build_options)
target_compile_definitions(spike_typography_kinetic_layout_test PRIVATE
  "INSTANTEE_PINNED_FONT_DIR=\"${PROJECT_SOURCE_DIR}/third_party/fonts\""
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_typography_kinetic_layout
         COMMAND spike_typography_kinetic_layout_test)
set_tests_properties(spike_typography_kinetic_layout PROPERTIES TIMEOUT 60
  LABELS "cpu;smoke")

add_executable(spike_typography_glyph_transform_test glyph_transform_test.cpp)
target_link_libraries(spike_typography_glyph_transform_test PRIVATE
  instantee::typography instantee_build_options)
add_test(NAME spike_typography_glyph_transform
         COMMAND spike_typography_glyph_transform_test)
set_tests_properties(spike_typography_glyph_transform PROPERTIES TIMEOUT 60
  LABELS "cpu;smoke")
