add_executable(spike_transfer_feature_matrix_test feature_matrix_test.mm)
target_compile_options(spike_transfer_feature_matrix_test PRIVATE -fobjc-arc)
target_link_libraries(spike_transfer_feature_matrix_test PRIVATE
  instantee::transfer_apple instantee_build_options)
add_test(NAME spike_transfer_feature_matrix
  COMMAND spike_transfer_feature_matrix_test)
set_tests_properties(spike_transfer_feature_matrix PROPERTIES TIMEOUT 60
  LABELS "cpu;smoke")

add_executable(spike_transfer_shared_surface_lifetime_test
  shared_surface_lifetime_test.mm)
target_compile_options(spike_transfer_shared_surface_lifetime_test PRIVATE
  -fobjc-arc)
target_link_libraries(spike_transfer_shared_surface_lifetime_test PRIVATE
  instantee::transfer_apple instantee_build_options)
add_test(NAME spike_transfer_shared_surface_lifetime
  COMMAND spike_transfer_shared_surface_lifetime_test)
set_tests_properties(spike_transfer_shared_surface_lifetime PROPERTIES
  TIMEOUT 120 LABELS "cpu;smoke")

# Composition of staging readback and the shared-surface path stays in the test
# target; render-metal and export-apple still do not depend on each other.
add_executable(spike_transfer_path_compare_test path_compare_test.mm)
target_compile_options(spike_transfer_path_compare_test PRIVATE -fobjc-arc)
target_link_libraries(spike_transfer_path_compare_test PRIVATE
  instantee::transfer_apple instantee::render_metal instantee::export_apple
  instantee_build_options)
add_test(NAME spike_transfer_path_compare
  COMMAND spike_transfer_path_compare_test)
set_tests_properties(spike_transfer_path_compare PROPERTIES TIMEOUT 180
  LABELS "cpu;smoke")
