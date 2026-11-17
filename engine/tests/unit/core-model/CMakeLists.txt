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
