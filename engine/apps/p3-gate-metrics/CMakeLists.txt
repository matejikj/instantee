add_executable(p3-gate-metrics main.mm)
target_compile_options(p3-gate-metrics PRIVATE -fobjc-arc)
target_link_libraries(p3-gate-metrics PRIVATE
  instantee::runtime
  instantee::export_core
  instantee::export_apple
  instantee::render_metal
  instantee::typography
  instantee::telemetry_apple
  instantee_build_options)
target_compile_definitions(p3-gate-metrics PRIVATE
  "INSTANTEE_PINNED_FONT_DIR=\"${PROJECT_SOURCE_DIR}/third_party/fonts\"")
if(INSTANTEE_SANITIZER STREQUAL "none")
  target_compile_definitions(p3-gate-metrics PRIVATE
    INSTANTEE_METRICS_VALIDATION="disabled")
else()
  target_compile_definitions(p3-gate-metrics PRIVATE
    INSTANTEE_METRICS_VALIDATION="sanitizer")
endif()
set_target_properties(p3-gate-metrics PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
