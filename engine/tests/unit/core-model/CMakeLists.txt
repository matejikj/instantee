add_executable(spike_core_model_time_test time_test.cpp)
target_link_libraries(spike_core_model_time_test PRIVATE instantee::core_model
  instantee_build_options)
add_test(NAME spike_core_model_time COMMAND spike_core_model_time_test)
set_tests_properties(spike_core_model_time PROPERTIES TIMEOUT 10 LABELS
  "cpu;smoke")
