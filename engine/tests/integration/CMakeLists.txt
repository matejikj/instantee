find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME spike_cli_smoke COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/cli_smoke.py"
  "$<TARGET_FILE:spike-render>" "$<TARGET_FILE:spike-benchmark>")
set_tests_properties(spike_cli_smoke PROPERTIES TIMEOUT 30 LABELS smoke)

add_executable(spike_cancellation_child cancellation_child.mm)
target_compile_options(spike_cancellation_child PRIVATE -fobjc-arc)
target_link_libraries(spike_cancellation_child PRIVATE instantee::runtime
  instantee::export_core instantee::export_apple instantee::render_metal
  instantee_build_options)
add_test(NAME spike_cancellation_subprocess COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/cancellation_subprocess.py"
  "$<TARGET_FILE:spike_cancellation_child>"
  "${PROJECT_SOURCE_DIR}/tools/workspace/supervise.py")
set_tests_properties(spike_cancellation_subprocess PROPERTIES TIMEOUT 180
  LABELS "cpu;smoke")
