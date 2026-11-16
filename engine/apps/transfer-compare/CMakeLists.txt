add_executable(spike-transfer-compare main.mm)
target_compile_options(spike-transfer-compare PRIVATE -fobjc-arc)
target_link_libraries(spike-transfer-compare PRIVATE
  instantee::transfer_apple instantee::render_metal instantee::export_apple
  instantee_build_options)
if(INSTANTEE_SANITIZER STREQUAL "none")
  target_compile_definitions(spike-transfer-compare PRIVATE
    INSTANTEE_TRANSFER_COMPARE_VALIDATION="disabled")
else()
  target_compile_definitions(spike-transfer-compare PRIVATE
    INSTANTEE_TRANSFER_COMPARE_VALIDATION="sanitizer")
endif()
set_target_properties(spike-transfer-compare PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
