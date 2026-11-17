add_executable(texture-atlas-bench main.cpp)
target_link_libraries(texture-atlas-bench PRIVATE instantee::asset_pipeline
  benchmark::benchmark instantee_build_options)
set_target_properties(texture-atlas-bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
