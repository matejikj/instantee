add_executable(spike_metal_device_test device_test.cpp)
target_link_libraries(spike_metal_device_test PRIVATE instantee::render_metal
  instantee_build_options)
add_test(NAME spike_metal_device COMMAND spike_metal_device_test)
set_tests_properties(spike_metal_device PROPERTIES TIMEOUT 30 LABELS "cpu;smoke")

add_executable(spike_metal_owner_test owner_test.cpp)
target_include_directories(spike_metal_owner_test PRIVATE
  "${PROJECT_SOURCE_DIR}/engine/src/render-metal/src")
target_include_directories(spike_metal_owner_test SYSTEM PRIVATE
  "${PROJECT_SOURCE_DIR}/third_party/metal-cpp")
target_link_libraries(spike_metal_owner_test PRIVATE instantee_build_options)
add_test(NAME spike_metal_owner COMMAND spike_metal_owner_test)
set_tests_properties(spike_metal_owner PROPERTIES TIMEOUT 10 LABELS cpu)
