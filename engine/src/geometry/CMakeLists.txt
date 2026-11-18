add_library(instantee_geometry STATIC
  path.cpp
  tessellate.cpp
  stroke.cpp
  path_cache.cpp
  arc_length.cpp)
add_library(instantee::geometry ALIAS instantee_geometry)
target_include_directories(instantee_geometry
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_geometry
  PUBLIC instantee::diagnostics
  PRIVATE instantee::libtess2 instantee_build_options)
instantee_mark_module(instantee_geometry production geometry)
