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
  src/ink_stroke.cpp
  src/plot_axes.cpp
  src/plot_curve.cpp
  src/diagram.cpp
  src/diagram_animation.cpp
  src/paper_notes.cpp
  src/paper_sheet.cpp
  src/paper_material.cpp
  src/notebook_accessories.cpp
  src/graphic_shapes.cpp
  src/editorial_charts.cpp
  src/shadow.cpp
  src/paper_layered_shadow.cpp
  src/paper_prop_blueprint.cpp
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
