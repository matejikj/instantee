add_executable(spike_apple_owner_test apple_owner_test.mm)
# Only this internal ownership test may inspect the adapter's private CF wrapper.
target_include_directories(spike_apple_owner_test PRIVATE "${INSTANTEE_ENGINE_SOURCE_DIR}/native/export-apple/src")
target_compile_options(spike_apple_owner_test PRIVATE -fobjc-arc)
target_link_libraries(spike_apple_owner_test PRIVATE instantee::export_apple instantee_build_options)
add_test(NAME spike_apple_owner COMMAND spike_apple_owner_test)
set_tests_properties(spike_apple_owner PROPERTIES TIMEOUT 10 LABELS cpu)

add_executable(spike_apple_pixel_buffer_pool_test pixel_buffer_pool_test.mm)
target_include_directories(spike_apple_pixel_buffer_pool_test PRIVATE
  "${INSTANTEE_ENGINE_SOURCE_DIR}/native/export-apple/src")
target_compile_options(spike_apple_pixel_buffer_pool_test PRIVATE -fobjc-arc)
target_link_libraries(spike_apple_pixel_buffer_pool_test PRIVATE
  instantee::export_apple instantee_build_options)
add_test(NAME spike_apple_pixel_buffer_pool
  COMMAND spike_apple_pixel_buffer_pool_test)
set_tests_properties(spike_apple_pixel_buffer_pool PROPERTIES TIMEOUT 60
  LABELS "cpu;smoke")

add_executable(spike_apple_video_writer_test video_writer_test.mm)
target_compile_options(spike_apple_video_writer_test PRIVATE -fobjc-arc)
target_link_libraries(spike_apple_video_writer_test PRIVATE
  instantee::export_apple instantee_build_options)
add_test(NAME spike_apple_video_writer COMMAND spike_apple_video_writer_test)
set_tests_properties(spike_apple_video_writer PROPERTIES TIMEOUT 120
  LABELS "cpu;smoke")

# Composition of Metal readback and the Apple writer stays in the test target;
# the backends do not depend on each other.
add_executable(spike_apple_render_to_mp4_test render_to_mp4_test.mm)
target_compile_options(spike_apple_render_to_mp4_test PRIVATE -fobjc-arc)
target_link_libraries(spike_apple_render_to_mp4_test PRIVATE
  instantee::export_apple instantee::render_metal instantee_build_options)
add_test(NAME spike_apple_render_to_mp4 COMMAND spike_apple_render_to_mp4_test)
set_tests_properties(spike_apple_render_to_mp4 PROPERTIES TIMEOUT 180
  LABELS "cpu;smoke")

add_executable(spike_apple_pts_timeline_test pts_timeline_test.mm)
target_compile_options(spike_apple_pts_timeline_test PRIVATE -fobjc-arc)
target_link_libraries(spike_apple_pts_timeline_test PRIVATE
  instantee::export_apple instantee::core_model instantee_build_options)
add_test(NAME spike_apple_pts_timeline COMMAND spike_apple_pts_timeline_test)
# 60 s + 300 s H.264 encodes. Label avoids the cpu|smoke benchmark preflight.
set_tests_properties(spike_apple_pts_timeline PROPERTIES TIMEOUT 1800
  LABELS pts)
