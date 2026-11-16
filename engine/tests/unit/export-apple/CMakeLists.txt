add_executable(spike_apple_owner_test apple_owner_test.mm)
# Only this internal ownership test may inspect the adapter's private CF wrapper.
target_include_directories(spike_apple_owner_test PRIVATE "${INSTANTEE_ENGINE_SOURCE_DIR}/native/export-apple/src")
target_compile_options(spike_apple_owner_test PRIVATE -fobjc-arc)
target_link_libraries(spike_apple_owner_test PRIVATE instantee::export_apple instantee_build_options)
add_test(NAME spike_apple_owner COMMAND spike_apple_owner_test)
set_tests_properties(spike_apple_owner PROPERTIES TIMEOUT 10 LABELS cpu)
