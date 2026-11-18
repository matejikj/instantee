add_executable(spike-b02-notebook-export main.mm)
target_compile_options(spike-b02-notebook-export PRIVATE -fobjc-arc)
target_link_libraries(spike-b02-notebook-export PRIVATE
  instantee::export
  instantee::export_apple
  instantee::compiler
  instantee::scene_ir
  instantee::runtime
  instantee::preview
  instantee::telemetry_apple
  instantee_build_options)
target_compile_definitions(spike-b02-notebook-export PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
if(INSTANTEE_SANITIZER STREQUAL "none")
  target_compile_definitions(spike-b02-notebook-export PRIVATE
    INSTANTEE_METRICS_VALIDATION="disabled")
else()
  target_compile_definitions(spike-b02-notebook-export PRIVATE
    INSTANTEE_METRICS_VALIDATION="sanitizer")
endif()
set_target_properties(spike-b02-notebook-export PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
