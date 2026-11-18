add_executable(paper-curvature-bench main.cpp)
target_link_libraries(paper-curvature-bench PRIVATE instantee::render_core
  benchmark::benchmark instantee_build_options)
set_target_properties(paper-curvature-bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
