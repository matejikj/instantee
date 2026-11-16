add_executable(spike-export-metrics main.mm)
target_compile_options(spike-export-metrics PRIVATE -fobjc-arc)
target_link_libraries(spike-export-metrics PRIVATE instantee::runtime
  instantee::export_core instantee::export_apple instantee::render_metal
  instantee::telemetry_apple instantee_build_options)
if(INSTANTEE_SANITIZER STREQUAL "none")
  target_compile_definitions(spike-export-metrics PRIVATE INSTANTEE_METRICS_VALIDATION="disabled")
else()
  target_compile_definitions(spike-export-metrics PRIVATE INSTANTEE_METRICS_VALIDATION="sanitizer")
endif()
set_target_properties(spike-export-metrics PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
