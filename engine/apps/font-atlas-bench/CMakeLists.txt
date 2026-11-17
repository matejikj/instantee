add_executable(font-atlas-bench main.cpp)
target_link_libraries(font-atlas-bench PRIVATE instantee::typography
  benchmark::benchmark instantee_build_options)
target_compile_definitions(font-atlas-bench PRIVATE
  "INSTANTEE_PINNED_FONT_DIR=\"${PROJECT_SOURCE_DIR}/third_party/fonts\"")
set_target_properties(font-atlas-bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
