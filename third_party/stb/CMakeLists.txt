# Header-only stb_image. Compile the implementation TU from asset-pipeline.
# This target only exports the include path.
add_library(instantee_stb_image INTERFACE)
add_library(instantee::stb_image ALIAS instantee_stb_image)
target_include_directories(instantee_stb_image
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
