add_executable(spike-b05-worst-accepted-export main.mm)
target_compile_options(spike-b05-worst-accepted-export PRIVATE -fobjc-arc)
target_link_libraries(spike-b05-worst-accepted-export PRIVATE
  instantee::export
  instantee::export_apple
  instantee::compiler
  instantee::scene_ir
  instantee::runtime
  instantee::preview
  instantee::telemetry_apple
  instantee_build_options)
target_compile_definitions(spike-b05-worst-accepted-export PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
if(INSTANTEE_SANITIZER STREQUAL "none")
  target_compile_definitions(spike-b05-worst-accepted-export PRIVATE
    INSTANTEE_METRICS_VALIDATION="disabled")
else()
  target_compile_definitions(spike-b05-worst-accepted-export PRIVATE
    INSTANTEE_METRICS_VALIDATION="sanitizer")
endif()
set_target_properties(spike-b05-worst-accepted-export PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
