add_library(instantee_core_model STATIC
  src/time.cpp
  src/ids.cpp
  src/integer_encoding.cpp
  src/time_convert.cpp)
add_library(instantee::core_model ALIAS instantee_core_model)
target_include_directories(instantee_core_model
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_core_model
  PUBLIC instantee::diagnostics
  PRIVATE instantee_build_options)
instantee_mark_module(instantee_core_model production core-model)
