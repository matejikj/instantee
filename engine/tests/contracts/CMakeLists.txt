# Compile the published headers without linking native Apple frameworks.
add_executable(spike_public_headers_test public_headers.cpp)
target_include_directories(spike_public_headers_test PRIVATE
  "$<TARGET_PROPERTY:instantee_export_apple,INTERFACE_INCLUDE_DIRECTORIES>"
  "$<TARGET_PROPERTY:instantee_render_metal,INTERFACE_INCLUDE_DIRECTORIES>"
  "$<TARGET_PROPERTY:instantee_runtime,INTERFACE_INCLUDE_DIRECTORIES>"
  "$<TARGET_PROPERTY:instantee_export_core,INTERFACE_INCLUDE_DIRECTORIES>"
  "$<TARGET_PROPERTY:instantee_telemetry_apple,INTERFACE_INCLUDE_DIRECTORIES>"
  "$<TARGET_PROPERTY:instantee_transfer_apple,INTERFACE_INCLUDE_DIRECTORIES>"
  "$<TARGET_PROPERTY:instantee_scene_ir,INTERFACE_INCLUDE_DIRECTORIES>"
  "$<TARGET_PROPERTY:instantee_compiler,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(spike_public_headers_test PRIVATE instantee::diagnostics
  instantee::core_model instantee::scene_ir instantee::compiler
  instantee_build_options)
add_test(NAME spike_public_headers COMMAND spike_public_headers_test)
set_tests_properties(spike_public_headers PROPERTIES TIMEOUT 10 LABELS cpu)

# One TU per public header catches missing includes that a shared TU can hide.
foreach(module_target IN ITEMS
    instantee_diagnostics
    instantee_core_model
    instantee_scene_ir
    instantee_compiler
    instantee_runtime
    instantee_export_core
    instantee_telemetry_apple
    instantee_render_metal
    instantee_export_apple
    instantee_transfer_apple)
  instantee_add_isolated_public_header_tests("${module_target}")
endforeach()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME module_boundaries
  COMMAND "${Python3_EXECUTABLE}"
          "${PROJECT_SOURCE_DIR}/tools/dev/check_module_boundaries.py"
          "--repo" "${PROJECT_SOURCE_DIR}")
set_tests_properties(module_boundaries PROPERTIES TIMEOUT 30 LABELS "cpu;boundaries")
