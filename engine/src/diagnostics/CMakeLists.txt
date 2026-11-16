add_library(instantee_diagnostics STATIC
  src/error.cpp
  src/exception_boundary.cpp)
add_library(instantee::diagnostics ALIAS instantee_diagnostics)
target_include_directories(instantee_diagnostics
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_diagnostics PRIVATE instantee_build_options)
instantee_mark_module(instantee_diagnostics production diagnostics)
