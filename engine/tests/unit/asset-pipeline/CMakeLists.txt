add_executable(spike_bitmap_decode_test bitmap_decode_test.cpp)
target_link_libraries(spike_bitmap_decode_test PRIVATE
  instantee::asset_pipeline instantee_build_options)
add_test(NAME spike_bitmap_decode COMMAND spike_bitmap_decode_test)
set_tests_properties(spike_bitmap_decode PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")
