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
