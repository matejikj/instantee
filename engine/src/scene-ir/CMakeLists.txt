add_library(instantee_scene_ir STATIC
  src/load.cpp
  src/uri_sandbox.cpp
  src/graph_validate.cpp)
add_library(instantee::scene_ir ALIAS instantee_scene_ir)
target_include_directories(instantee_scene_ir
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_scene_ir
  PUBLIC instantee::diagnostics instantee::core_model
  PRIVATE instantee::yyjson instantee_build_options)
instantee_mark_module(instantee_scene_ir production scene-ir)
