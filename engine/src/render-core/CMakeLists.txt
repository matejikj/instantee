add_library(instantee_render_core STATIC
  src/renderer.cpp
  src/render_graph.cpp
  src/resource_registry.cpp
  src/render_target_pool.cpp
  src/staging_pool.cpp
  src/shader_features.cpp
  src/pipeline_cache.cpp
  src/primitives.cpp
  src/clip_compositor.cpp
  src/photo_crop.cpp
  src/halftone.cpp
  src/print_grain.cpp
  src/graphic_shapes.cpp
  src/editorial_charts.cpp
  src/shadow.cpp
  src/linear_compositor.cpp
  src/visibility.cpp
  src/frame_primitives.cpp
  src/instance_batch.cpp)
add_library(instantee::render_core ALIAS instantee_render_core)
target_include_directories(instantee_render_core
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_render_core
  PUBLIC instantee::diagnostics instantee::core_model
  PRIVATE instantee_build_options)
instantee_mark_module(instantee_render_core production render-core)
