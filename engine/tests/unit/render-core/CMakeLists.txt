add_executable(spike_render_core_port_test render_core_port_test.cpp)
target_link_libraries(spike_render_core_port_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_render_core_port COMMAND spike_render_core_port_test)
set_tests_properties(spike_render_core_port PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_render_graph_test render_graph_test.cpp)
target_link_libraries(spike_render_graph_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_render_graph COMMAND spike_render_graph_test)
set_tests_properties(spike_render_graph PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_resource_registry_test resource_registry_test.cpp)
target_link_libraries(spike_resource_registry_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_resource_registry COMMAND spike_resource_registry_test)
set_tests_properties(spike_resource_registry PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_render_target_pool_test render_target_pool_test.cpp)
target_link_libraries(spike_render_target_pool_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_render_target_pool COMMAND spike_render_target_pool_test)
set_tests_properties(spike_render_target_pool PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_staging_pool_test staging_pool_test.cpp)
target_link_libraries(spike_staging_pool_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_staging_pool COMMAND spike_staging_pool_test)
set_tests_properties(spike_staging_pool PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_shader_features_test shader_features_test.cpp)
target_link_libraries(spike_shader_features_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_shader_features COMMAND spike_shader_features_test)
set_tests_properties(spike_shader_features PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_pipeline_cache_test pipeline_cache_test.cpp)
target_link_libraries(spike_pipeline_cache_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_pipeline_cache COMMAND spike_pipeline_cache_test)
set_tests_properties(spike_pipeline_cache PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_primitives_test primitives_test.cpp)
target_link_libraries(spike_primitives_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_primitives COMMAND spike_primitives_test)
set_tests_properties(spike_primitives PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_instance_batch_test instance_batch_test.cpp)
target_link_libraries(spike_instance_batch_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_instance_batch COMMAND spike_instance_batch_test)
set_tests_properties(spike_instance_batch PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_clip_compositor_test clip_compositor_test.cpp)
target_link_libraries(spike_clip_compositor_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_clip_compositor COMMAND spike_clip_compositor_test)
set_tests_properties(spike_clip_compositor PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_photo_crop_test photo_crop_test.cpp)
target_link_libraries(spike_photo_crop_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_photo_crop
  COMMAND spike_photo_crop_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_photo_crop PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_halftone_test halftone_test.cpp)
target_link_libraries(spike_halftone_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_halftone
  COMMAND spike_halftone_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_halftone PROPERTIES TIMEOUT 30 LABELS "cpu;smoke")

add_executable(spike_print_grain_test print_grain_test.cpp)
target_link_libraries(spike_print_grain_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_print_grain
  COMMAND spike_print_grain_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_print_grain PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_ink_stroke_feature_test ink_stroke_feature_test.cpp)
target_link_libraries(spike_ink_stroke_feature_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_ink_stroke_feature COMMAND spike_ink_stroke_feature_test)
set_tests_properties(spike_ink_stroke_feature PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_plot_axes_test plot_axes_test.cpp)
target_link_libraries(spike_plot_axes_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_plot_axes COMMAND spike_plot_axes_test)
set_tests_properties(spike_plot_axes PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_graphic_shapes_test graphic_shapes_test.cpp)
target_link_libraries(spike_graphic_shapes_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_graphic_shapes
  COMMAND spike_graphic_shapes_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_graphic_shapes PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_editorial_charts_test editorial_charts_test.cpp)
target_link_libraries(spike_editorial_charts_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_editorial_charts
  COMMAND spike_editorial_charts_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_editorial_charts PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_shadow_test shadow_test.cpp)
target_link_libraries(spike_shadow_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_shadow COMMAND spike_shadow_test)
set_tests_properties(spike_shadow PROPERTIES TIMEOUT 30 LABELS "cpu;smoke")

add_executable(spike_linear_compositor_test linear_compositor_test.cpp)
target_link_libraries(spike_linear_compositor_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_linear_compositor COMMAND spike_linear_compositor_test)
set_tests_properties(spike_linear_compositor PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_visibility_test visibility_test.cpp)
target_link_libraries(spike_visibility_test PRIVATE
  instantee::render_core instantee_build_options)
add_test(NAME spike_visibility COMMAND spike_visibility_test)
set_tests_properties(spike_visibility PROPERTIES TIMEOUT 30 LABELS "cpu;smoke")
