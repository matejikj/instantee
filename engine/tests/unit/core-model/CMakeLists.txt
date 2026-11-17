add_executable(spike_core_model_time_test time_test.cpp)
target_link_libraries(spike_core_model_time_test PRIVATE instantee::core_model
  instantee_build_options)
add_test(NAME spike_core_model_time COMMAND spike_core_model_time_test)
set_tests_properties(spike_core_model_time PROPERTIES TIMEOUT 10 LABELS
  "cpu;smoke")

add_executable(spike_core_model_ids_test ids_test.cpp)
target_link_libraries(spike_core_model_ids_test PRIVATE instantee::core_model
  instantee_build_options)
add_test(NAME spike_core_model_ids COMMAND spike_core_model_ids_test)
set_tests_properties(spike_core_model_ids PROPERTIES TIMEOUT 10 LABELS
  "cpu;smoke")

add_executable(spike_core_model_integer_encoding_test integer_encoding_test.cpp)
target_link_libraries(spike_core_model_integer_encoding_test PRIVATE
  instantee::core_model instantee_build_options)
add_test(NAME spike_core_model_integer_encoding
  COMMAND spike_core_model_integer_encoding_test)
set_tests_properties(spike_core_model_integer_encoding PROPERTIES TIMEOUT 10
  LABELS "cpu;smoke")

add_executable(spike_core_model_time_convert_test time_convert_test.cpp)
target_link_libraries(spike_core_model_time_convert_test PRIVATE
  instantee::core_model instantee_build_options)
add_test(NAME spike_core_model_time_convert
  COMMAND spike_core_model_time_convert_test)
set_tests_properties(spike_core_model_time_convert PROPERTIES TIMEOUT 10
  LABELS "cpu;smoke")

add_executable(spike_core_model_transform_test transform_test.cpp)
target_link_libraries(spike_core_model_transform_test PRIVATE
  instantee::core_model instantee_build_options)
add_test(NAME spike_core_model_transform COMMAND spike_core_model_transform_test)
set_tests_properties(spike_core_model_transform PROPERTIES TIMEOUT 10 LABELS
  "cpu;smoke")

add_executable(spike_core_model_camera_test camera_test.cpp)
target_link_libraries(spike_core_model_camera_test PRIVATE
  instantee::core_model instantee_build_options)
add_test(NAME spike_core_model_camera COMMAND spike_core_model_camera_test)
set_tests_properties(spike_core_model_camera PROPERTIES TIMEOUT 10 LABELS
  "cpu;smoke")

add_executable(spike_core_model_camera_depth_test camera_depth_test.cpp)
target_link_libraries(spike_core_model_camera_depth_test PRIVATE
  instantee::core_model instantee_build_options)
add_test(NAME spike_core_model_camera_depth
  COMMAND spike_core_model_camera_depth_test)
set_tests_properties(spike_core_model_camera_depth PROPERTIES TIMEOUT 10 LABELS
  "cpu;smoke")

add_executable(spike_core_model_properties_test properties_test.cpp)
target_link_libraries(spike_core_model_properties_test PRIVATE
  instantee::core_model instantee_build_options)
add_test(NAME spike_core_model_properties
  COMMAND spike_core_model_properties_test)
set_tests_properties(spike_core_model_properties PROPERTIES TIMEOUT 10 LABELS
  "cpu;smoke")
