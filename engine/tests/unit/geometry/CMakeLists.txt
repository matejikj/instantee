add_executable(spike_path_tessellation_test path_tessellation_test.cpp)
target_link_libraries(spike_path_tessellation_test PRIVATE
  instantee::geometry instantee_build_options)
add_test(NAME spike_path_tessellation COMMAND spike_path_tessellation_test)
set_tests_properties(spike_path_tessellation PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_arc_length_test arc_length_test.cpp)
target_link_libraries(spike_arc_length_test PRIVATE
  instantee::geometry instantee_build_options)
add_test(NAME spike_arc_length COMMAND spike_arc_length_test)
set_tests_properties(spike_arc_length PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")
