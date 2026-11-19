find_library(INSTANTEE_FOUNDATION Foundation REQUIRED)
find_library(INSTANTEE_CORE_FOUNDATION CoreFoundation REQUIRED)
find_library(INSTANTEE_CORE_VIDEO CoreVideo REQUIRED)
find_library(INSTANTEE_AVFOUNDATION AVFoundation REQUIRED)
find_library(INSTANTEE_CORE_MEDIA CoreMedia REQUIRED)

add_library(instantee_export_apple STATIC
  src/workspace.mm
  src/pixel_buffer.mm
  src/color_path.mm
  src/video_writer.mm
  src/media_conformance.cpp
  src/color_accuracy.cpp
  src/adapter_compatibility.mm
  src/apple_video_sink.cpp)
add_library(instantee::export_apple ALIAS instantee_export_apple)
target_include_directories(instantee_export_apple PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(instantee_export_apple PRIVATE -fobjc-arc)
target_link_libraries(instantee_export_apple
  PUBLIC instantee::diagnostics instantee::core_model instantee::export_core
  PRIVATE instantee_build_options
    "${INSTANTEE_FOUNDATION}"
    "${INSTANTEE_CORE_FOUNDATION}"
    "${INSTANTEE_CORE_VIDEO}"
    "${INSTANTEE_AVFOUNDATION}"
    "${INSTANTEE_CORE_MEDIA}")
# Version text only — no host/user identity. Mirrors transfer-apple probe macros.
get_filename_component(_instantee_export_sdk_name "${CMAKE_OSX_SYSROOT}" NAME)
target_compile_definitions(instantee_export_apple PRIVATE
  "INSTANTEE_EXPORT_DEPLOYMENT_TARGET=\"${CMAKE_OSX_DEPLOYMENT_TARGET}\""
  "INSTANTEE_EXPORT_SDK_VERSION=\"${_instantee_export_sdk_name}\""
  "INSTANTEE_EXPORT_XCODE_VERSION_LOCK=\"${INSTANTEE_SDK_VERSION}\"")
instantee_mark_module(instantee_export_apple spike export-apple)
