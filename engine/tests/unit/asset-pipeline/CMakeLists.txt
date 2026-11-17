add_executable(spike_bitmap_decode_test bitmap_decode_test.cpp)
target_link_libraries(spike_bitmap_decode_test PRIVATE
  instantee::asset_pipeline instantee_build_options)
add_test(NAME spike_bitmap_decode COMMAND spike_bitmap_decode_test)
set_tests_properties(spike_bitmap_decode PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_svg_import_test svg_import_test.cpp)
target_link_libraries(spike_svg_import_test PRIVATE
  instantee::asset_pipeline instantee_build_options)
add_test(NAME spike_svg_import COMMAND spike_svg_import_test)
set_tests_properties(spike_svg_import PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_asset_cache_test asset_cache_test.cpp)
target_link_libraries(spike_asset_cache_test PRIVATE
  instantee::asset_pipeline instantee_build_options)
add_test(NAME spike_asset_cache COMMAND spike_asset_cache_test)
set_tests_properties(spike_asset_cache PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_texture_atlas_test texture_atlas_test.cpp)
target_link_libraries(spike_texture_atlas_test PRIVATE
  instantee::asset_pipeline instantee_build_options)
add_test(NAME spike_texture_atlas COMMAND spike_texture_atlas_test)
set_tests_properties(spike_texture_atlas PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")
