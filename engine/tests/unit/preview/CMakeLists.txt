add_executable(spike_preview_frame_png_test frame_png_test.cpp)
target_link_libraries(spike_preview_frame_png_test PRIVATE
  instantee::preview instantee_build_options)
add_test(NAME spike_preview_frame_png COMMAND spike_preview_frame_png_test)
set_tests_properties(spike_preview_frame_png PROPERTIES TIMEOUT 60 LABELS
  "cpu;smoke")
