add_library(instantee_runtime STATIC
  src/frame_credits.cpp
  src/stop_signals.cpp
  src/frame_samples.cpp
  src/compiled_scene.cpp
  src/transforms.cpp
  src/layering.cpp)
add_library(instantee::runtime ALIAS instantee_runtime)
target_include_directories(instantee_runtime
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_runtime
  PUBLIC instantee::diagnostics instantee::core_model
  PRIVATE instantee_build_options)
instantee_mark_module(instantee_runtime production runtime)
