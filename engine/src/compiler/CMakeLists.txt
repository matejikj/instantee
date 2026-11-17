add_library(instantee_compiler STATIC src/compile.cpp)
add_library(instantee::compiler ALIAS instantee_compiler)
target_include_directories(instantee_compiler
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_compiler
  PUBLIC instantee::diagnostics instantee::core_model instantee::scene_ir
  PRIVATE instantee_build_options)
instantee_mark_module(instantee_compiler production compiler)
