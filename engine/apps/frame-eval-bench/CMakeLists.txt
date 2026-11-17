add_executable(frame-eval-bench main.cpp)
target_link_libraries(frame-eval-bench PRIVATE instantee::runtime
  benchmark::benchmark instantee_build_options)
set_target_properties(frame-eval-bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
