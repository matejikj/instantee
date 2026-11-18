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

add_executable(spike_ink_stroke_test ink_stroke_test.cpp)
target_link_libraries(spike_ink_stroke_test PRIVATE
  instantee::geometry instantee_build_options)
add_test(NAME spike_ink_stroke
  COMMAND spike_ink_stroke_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_ink_stroke PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_paper_cut_test paper_cut_test.cpp)
target_link_libraries(spike_paper_cut_test PRIVATE
  instantee::geometry instantee_build_options)
add_test(NAME spike_paper_cut
  COMMAND spike_paper_cut_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_paper_cut PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")
