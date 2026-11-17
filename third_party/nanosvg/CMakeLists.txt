# Header-only NanoSVG. The implementation TU lives in asset-pipeline.
add_library(instantee_nanosvg INTERFACE)
add_library(instantee::nanosvg ALIAS instantee_nanosvg)
target_include_directories(instantee_nanosvg
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
