add_executable(spike_diagnostics_error_test error_test.cpp)
target_link_libraries(spike_diagnostics_error_test PRIVATE
  instantee::diagnostics instantee_build_options)
add_test(NAME spike_diagnostics_error COMMAND spike_diagnostics_error_test)
set_tests_properties(spike_diagnostics_error PROPERTIES TIMEOUT 10 LABELS
  "cpu;smoke")
