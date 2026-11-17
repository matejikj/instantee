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
