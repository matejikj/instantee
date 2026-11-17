add_executable(spike_runtime_backpressure_test backpressure_test.cpp)
target_link_libraries(spike_runtime_backpressure_test PRIVATE instantee::runtime
  instantee_build_options)
add_test(NAME spike_runtime_backpressure COMMAND spike_runtime_backpressure_test)
set_tests_properties(spike_runtime_backpressure PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_runtime_cancellation_test cancellation_test.cpp)
target_link_libraries(spike_runtime_cancellation_test PRIVATE instantee::runtime
  instantee_build_options)
add_test(NAME spike_runtime_cancellation COMMAND spike_runtime_cancellation_test)
set_tests_properties(spike_runtime_cancellation PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_frame_samples_test frame_samples_test.cpp)
target_link_libraries(spike_frame_samples_test PRIVATE instantee::runtime instantee_build_options)
add_test(NAME spike_frame_samples COMMAND spike_frame_samples_test)
set_tests_properties(spike_frame_samples PROPERTIES TIMEOUT 10 LABELS cpu)

add_executable(spike_runtime_compiled_scene_test compiled_scene_test.cpp)
target_link_libraries(spike_runtime_compiled_scene_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_compiled_scene
  COMMAND spike_runtime_compiled_scene_test)
set_tests_properties(spike_runtime_compiled_scene PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_runtime_transforms_test transforms_test.cpp)
target_link_libraries(spike_runtime_transforms_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_transforms COMMAND spike_runtime_transforms_test)
set_tests_properties(spike_runtime_transforms PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_runtime_layering_test layering_test.cpp)
target_link_libraries(spike_runtime_layering_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_layering COMMAND spike_runtime_layering_test)
set_tests_properties(spike_runtime_layering PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_runtime_timeline_test timeline_test.cpp)
target_link_libraries(spike_runtime_timeline_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_timeline COMMAND spike_runtime_timeline_test)
set_tests_properties(spike_runtime_timeline PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")
