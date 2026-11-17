# Pinned yyjson amalgamation. Keep PRIVATE to scene-ir; do not re-export headers.
add_library(instantee_yyjson STATIC yyjson.c)
add_library(instantee::yyjson ALIAS instantee_yyjson)
target_include_directories(instantee_yyjson PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# Bound nesting before DOM allocation; matches contracts security limits.
target_compile_definitions(instantee_yyjson PUBLIC YYJSON_READER_DEPTH_LIMIT=64)
set_target_properties(instantee_yyjson PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
