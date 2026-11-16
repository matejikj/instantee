include("${PROJECT_SOURCE_DIR}/cmake/EmbedTextAsHeader.cmake")
set(INSTANTEE_SHARED_SURFACE_SHADER
  "${PROJECT_SOURCE_DIR}/engine/shaders/spike/shared_surface_pattern.metal")
set(INSTANTEE_SHARED_SURFACE_SOURCE_HEADER
  "${CMAKE_CURRENT_BINARY_DIR}/generated/shared_surface_pattern_source.hpp")
instantee_embed_text_as_header(
  "${INSTANTEE_SHARED_SURFACE_SHADER}"
  "${INSTANTEE_SHARED_SURFACE_SOURCE_HEADER}"
  "instantee::spike::transfer::detail"
  "kSharedSurfacePatternSource")

set(INSTANTEE_METAL_CPP_ROOT "${PROJECT_SOURCE_DIR}/third_party/metal-cpp")

find_library(INSTANTEE_TRANSFER_FOUNDATION Foundation REQUIRED)
find_library(INSTANTEE_TRANSFER_CORE_FOUNDATION CoreFoundation REQUIRED)
find_library(INSTANTEE_TRANSFER_CORE_VIDEO CoreVideo REQUIRED)
find_library(INSTANTEE_TRANSFER_IOSURFACE IOSurface REQUIRED)
find_library(INSTANTEE_TRANSFER_METAL Metal REQUIRED)

add_library(instantee_transfer_apple STATIC
  src/feature_matrix.mm
  src/shared_surface.mm)
add_library(instantee::transfer_apple ALIAS instantee_transfer_apple)
target_include_directories(instantee_transfer_apple
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${PROJECT_SOURCE_DIR}/engine/src/render-metal/src"
    "${PROJECT_SOURCE_DIR}/engine/native/export-apple/src")
target_include_directories(instantee_transfer_apple SYSTEM PRIVATE
  "${INSTANTEE_METAL_CPP_ROOT}")
target_compile_options(instantee_transfer_apple PRIVATE -fobjc-arc)
# Record SDK file name only; never embed absolute developer paths.
get_filename_component(_instantee_transfer_sdk_name "${CMAKE_OSX_SYSROOT}" NAME)
target_compile_definitions(instantee_transfer_apple PRIVATE
  "INSTANTEE_TRANSFER_DEPLOYMENT_TARGET=\"${CMAKE_OSX_DEPLOYMENT_TARGET}\""
  "INSTANTEE_TRANSFER_SDK_VERSION=\"${_instantee_transfer_sdk_name}\"")
target_link_libraries(instantee_transfer_apple
  PUBLIC instantee::diagnostics instantee::render_metal instantee::export_apple
  PRIVATE instantee_build_options
    "${INSTANTEE_TRANSFER_FOUNDATION}"
    "${INSTANTEE_TRANSFER_CORE_FOUNDATION}"
    "${INSTANTEE_TRANSFER_CORE_VIDEO}"
    "${INSTANTEE_TRANSFER_IOSURFACE}"
    "${INSTANTEE_TRANSFER_METAL}")
