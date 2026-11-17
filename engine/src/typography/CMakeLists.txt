include("${PROJECT_SOURCE_DIR}/cmake/FindTypographyDeps.cmake")

add_library(instantee_typography STATIC
  src/text_shape.cpp
  src/glyph_atlas.cpp)
add_library(instantee::typography ALIAS instantee_typography)
target_include_directories(instantee_typography
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_typography
  PUBLIC instantee::diagnostics instantee::asset_pipeline
  PRIVATE
    PkgConfig::INSTANTEE_HARFBUZZ
    PkgConfig::INSTANTEE_FREETYPE
    PkgConfig::INSTANTEE_ICU_UC
    instantee_build_options)
instantee_mark_module(instantee_typography production typography)
