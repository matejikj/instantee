# Compile the published headers without linking native code or Apple frameworks.
add_executable(spike_public_headers_test public_headers.cpp)
target_include_directories(spike_public_headers_test PRIVATE
  "$<TARGET_PROPERTY:instantee_export_apple,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(spike_public_headers_test PRIVATE instantee::diagnostics instantee_build_options)
add_test(NAME spike_public_headers COMMAND spike_public_headers_test)
set_tests_properties(spike_public_headers PROPERTIES TIMEOUT 10 LABELS cpu)
