# One ordered command sequence: CTest failure prevents starting the child.
# This entry point performs no timing; a future harness owns spawn-to-wait metrics.
# Build the whole tree first so every cpu|smoke CTest binary exists (the label
# set grows with new modules; enumerating DEPENDS here drifts quickly).
if(INSTANTEE_SANITIZER STREQUAL "none" AND CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
  add_custom_target(run-spike-benchmark
    COMMAND "${CMAKE_COMMAND}" --build "${PROJECT_BINARY_DIR}"
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${PROJECT_BINARY_DIR}"
      --output-on-failure --no-tests=error -L "cpu|smoke"
    COMMAND "$<TARGET_FILE:spike-benchmark>"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    DEPENDS spike-benchmark
      ${INSTANTEE_TOOLCHAIN_PROBE_TARGETS}
    USES_TERMINAL VERBATIM)
endif()
