add_library(instantee_runtime STATIC src/frame_credits.cpp src/stop_signals.cpp src/frame_samples.cpp)
add_library(instantee::runtime ALIAS instantee_runtime)
target_include_directories(instantee_runtime
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_runtime
  PUBLIC instantee::diagnostics
  PRIVATE instantee_build_options)
