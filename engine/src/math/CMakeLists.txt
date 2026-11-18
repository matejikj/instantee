add_library(instantee_math STATIC
  src/grammar.cpp
  src/ast.cpp
  src/parse.cpp)
add_library(instantee::math ALIAS instantee_math)
target_include_directories(instantee_math
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_math
  PUBLIC instantee::diagnostics
  PRIVATE instantee_build_options)
instantee_mark_module(instantee_math production math)
