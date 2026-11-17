add_library(instantee_export STATIC
  src/scene_video_export.cpp)
add_library(instantee::export ALIAS instantee_export)
target_include_directories(instantee_export
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_export
  PUBLIC instantee::diagnostics instantee::core_model instantee::export_core
         instantee::runtime instantee::preview
  PRIVATE instantee_build_options)
instantee_mark_module(instantee_export production export)
