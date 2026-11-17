add_executable(instantee main.cpp)
target_link_libraries(instantee PRIVATE instantee::compiler instantee::scene_ir
  instantee::diagnostics instantee_build_options)
set_target_properties(instantee PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
