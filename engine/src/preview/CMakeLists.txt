find_library(INSTANTEE_PREVIEW_COMPRESSION_FRAMEWORK Compression REQUIRED)

add_library(instantee_preview STATIC
  src/frame_png.cpp
  src/png_encode.cpp)
add_library(instantee::preview ALIAS instantee_preview)
target_include_directories(instantee_preview
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_preview
  PUBLIC instantee::diagnostics instantee::core_model instantee::runtime
         instantee::render_core
  PRIVATE instantee_build_options
          "${INSTANTEE_PREVIEW_COMPRESSION_FRAMEWORK}")
instantee_mark_module(instantee_preview production preview)
