# One ordered command sequence: CTest failure prevents starting the child.
# This entry point performs no timing; a future harness owns spawn-to-wait metrics.
if(INSTANTEE_SANITIZER STREQUAL "none" AND CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
  add_custom_target(run-spike-benchmark
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${PROJECT_BINARY_DIR}"
      --output-on-failure --no-tests=error -L "cpu|smoke"
    COMMAND "$<TARGET_FILE:spike-benchmark>"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    DEPENDS spike-render spike-benchmark spike_cli_test spike_core_model_time_test
      spike_runtime_backpressure_test spike_export_core_transaction_test
      spike_apple_owner_test spike_apple_pixel_buffer_pool_test
      spike_apple_video_writer_test spike_apple_render_to_mp4_test
      spike_apple_color_roundtrip_test
      spike_metal_device_test spike_metal_owner_test spike_metal_pattern_test
      spike_metal_readback_lifetime_test spike_public_headers_test
      ${INSTANTEE_TOOLCHAIN_PROBE_TARGETS}
    USES_TERMINAL VERBATIM)
endif()
