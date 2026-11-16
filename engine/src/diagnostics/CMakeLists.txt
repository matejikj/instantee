add_library(instantee_diagnostics INTERFACE)
add_library(instantee::diagnostics ALIAS instantee_diagnostics)
target_include_directories(instantee_diagnostics INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
