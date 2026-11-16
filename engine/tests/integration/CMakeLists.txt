find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME spike_cli_smoke COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/cli_smoke.py"
  "$<TARGET_FILE:spike-render>" "$<TARGET_FILE:spike-benchmark>")
set_tests_properties(spike_cli_smoke PROPERTIES TIMEOUT 30 LABELS smoke)
