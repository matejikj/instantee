# The parser is application implementation, not a domain or adapter dependency.
add_library(instantee_spike_cli STATIC cli.cpp)
target_link_libraries(instantee_spike_cli PUBLIC instantee::diagnostics
  PRIVATE instantee_build_options)

foreach(tool IN ITEMS spike-render spike-benchmark)
  add_executable(${tool} main.cpp)
  target_link_libraries(${tool} PRIVATE instantee_spike_cli instantee::export_apple
    instantee::render_metal instantee_build_options)
  set_target_properties(${tool} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
endforeach()
target_compile_definitions(spike-benchmark PRIVATE INSTANTEE_SPIKE_BENCHMARK=1)
