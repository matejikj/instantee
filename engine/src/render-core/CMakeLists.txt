add_library(instantee_render_core STATIC
  src/renderer.cpp
  src/render_graph.cpp)
add_library(instantee::render_core ALIAS instantee_render_core)
target_include_directories(instantee_render_core
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_render_core
  PUBLIC instantee::diagnostics
  PRIVATE instantee_build_options)
instantee_mark_module(instantee_render_core production render-core)
