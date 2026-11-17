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
