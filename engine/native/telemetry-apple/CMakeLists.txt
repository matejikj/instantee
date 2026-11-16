add_library(instantee_telemetry_apple STATIC src/process_sampler.cpp)
add_library(instantee::telemetry_apple ALIAS instantee_telemetry_apple)
target_include_directories(instantee_telemetry_apple PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_telemetry_apple PUBLIC instantee::diagnostics
  PRIVATE instantee_build_options)
