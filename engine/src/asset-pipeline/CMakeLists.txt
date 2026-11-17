add_library(instantee_asset_pipeline STATIC
  src/bitmap_decode.cpp
  src/stb_image_impl.cpp
  src/svg_security.cpp
  src/svg_import.cpp
  src/nanosvg_impl.cpp
  src/asset_cache.cpp
  src/texture_atlas.cpp)
add_library(instantee::asset_pipeline ALIAS instantee_asset_pipeline)
target_include_directories(instantee_asset_pipeline
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_asset_pipeline
  PUBLIC instantee::diagnostics instantee::geometry
  PRIVATE instantee::stb_image instantee::libwebp_decode instantee::nanosvg
          instantee_build_options
          "-framework Security")
instantee_mark_module(instantee_asset_pipeline production asset-pipeline)
