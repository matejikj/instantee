add_executable(instance-batch-bench main.cpp)
target_link_libraries(instance-batch-bench PRIVATE instantee::render_core
  benchmark::benchmark instantee_build_options)
set_target_properties(instance-batch-bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
