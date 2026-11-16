set(INSTANTEE_SANITIZER "none" CACHE STRING "Diagnostic instrumentation")
set_property(CACHE INSTANTEE_SANITIZER PROPERTY STRINGS none address-undefined thread)
add_library(instantee_build_options INTERFACE)
target_compile_options(instantee_build_options INTERFACE -Wall -Wextra -Werror -pedantic-errors)
if(INSTANTEE_SANITIZER STREQUAL "address-undefined")
  set(sanitizer_flags -fsanitize=address,undefined -fno-omit-frame-pointer)
elseif(INSTANTEE_SANITIZER STREQUAL "thread")
  set(sanitizer_flags -fsanitize=thread -fno-omit-frame-pointer)
elseif(NOT INSTANTEE_SANITIZER STREQUAL "none")
  message(FATAL_ERROR "Unsupported INSTANTEE_SANITIZER")
endif()
target_compile_options(instantee_build_options INTERFACE ${sanitizer_flags})
target_link_options(instantee_build_options INTERFACE ${sanitizer_flags})

add_library(instantee_spike_core STATIC src/core/cli.cpp)
target_include_directories(instantee_spike_core PUBLIC src/core)
target_link_libraries(instantee_spike_core PRIVATE instantee_build_options)

find_library(INSTANTEE_FOUNDATION Foundation REQUIRED)
find_library(INSTANTEE_CORE_FOUNDATION CoreFoundation REQUIRED)
add_library(instantee_export_apple STATIC native/export-apple/workspace.mm)
target_include_directories(instantee_export_apple PUBLIC native/export-apple)
target_compile_options(instantee_export_apple PRIVATE -fobjc-arc)
target_link_libraries(instantee_export_apple PUBLIC instantee_spike_core
  PRIVATE instantee_build_options "${INSTANTEE_FOUNDATION}" "${INSTANTEE_CORE_FOUNDATION}")

foreach(tool IN ITEMS spike-render spike-benchmark)
  add_executable(${tool} apps/cli/main.cpp)
  target_link_libraries(${tool} PRIVATE instantee_export_apple instantee_build_options)
endforeach()
target_compile_definitions(spike-benchmark PRIVATE INSTANTEE_SPIKE_BENCHMARK=1)

add_executable(spike_core_test tests/unit/cli_test.cpp)
target_link_libraries(spike_core_test PRIVATE instantee_spike_core instantee_build_options)
add_test(NAME spike_core COMMAND spike_core_test)
set_tests_properties(spike_core PROPERTIES TIMEOUT 10 LABELS cpu)

add_executable(spike_apple_owner_test tests/unit/apple_owner_test.mm)
target_compile_options(spike_apple_owner_test PRIVATE -fobjc-arc)
target_link_libraries(spike_apple_owner_test PRIVATE instantee_export_apple instantee_build_options)
add_test(NAME spike_apple_owner COMMAND spike_apple_owner_test)
set_tests_properties(spike_apple_owner PROPERTIES TIMEOUT 10 LABELS cpu)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME spike_cli_smoke COMMAND "${Python3_EXECUTABLE}"
  "${PROJECT_SOURCE_DIR}/tests/integration/cli_smoke.py"
  "$<TARGET_FILE:spike-render>" "$<TARGET_FILE:spike-benchmark>")
set_tests_properties(spike_cli_smoke PROPERTIES TIMEOUT 30 LABELS smoke)

# One ordered command sequence: CTest failure prevents starting the child.
# This entry point performs no timing; a future harness owns spawn-to-wait metrics.
if(INSTANTEE_SANITIZER STREQUAL "none" AND CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
  add_custom_target(run-spike-benchmark
    COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure --no-tests=error -L "cpu|smoke"
    COMMAND "$<TARGET_FILE:spike-benchmark>"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    DEPENDS spike-render spike-benchmark spike_core_test spike_apple_owner_test
    USES_TERMINAL VERBATIM)
endif()
