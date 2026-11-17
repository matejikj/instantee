add_executable(contract-seed-check main.cpp)
target_link_libraries(contract-seed-check PRIVATE instantee::core_model
  instantee_build_options)
set_target_properties(contract-seed-check PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
