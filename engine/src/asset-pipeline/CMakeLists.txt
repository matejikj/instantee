add_library(instantee_asset_pipeline STATIC
  src/bitmap_decode.cpp
  src/stb_image_impl.cpp)
add_library(instantee::asset_pipeline ALIAS instantee_asset_pipeline)
target_include_directories(instantee_asset_pipeline
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_asset_pipeline
  PUBLIC instantee::diagnostics
  PRIVATE instantee::stb_image instantee::libwebp_decode instantee_build_options)
instantee_mark_module(instantee_asset_pipeline production asset-pipeline)
