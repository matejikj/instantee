set(INSTANTEE_METAL_CPP_ROOT "${PROJECT_SOURCE_DIR}/third_party/metal-cpp")
set(INSTANTEE_METAL_CPP_PIN "${INSTANTEE_METAL_CPP_ROOT}/PIN.json")
if(NOT EXISTS "${INSTANTEE_METAL_CPP_PIN}")
  message(FATAL_ERROR "Pinned metal-cpp metadata is missing: ${INSTANTEE_METAL_CPP_PIN}")
endif()
if(NOT EXISTS "${INSTANTEE_METAL_CPP_ROOT}/Metal/Metal.hpp")
  message(FATAL_ERROR "Pinned metal-cpp headers are missing under ${INSTANTEE_METAL_CPP_ROOT}")
endif()

file(READ "${INSTANTEE_METAL_CPP_PIN}" INSTANTEE_METAL_CPP_PIN_JSON)
string(JSON INSTANTEE_METAL_CPP_TAG GET "${INSTANTEE_METAL_CPP_PIN_JSON}" tag)
string(JSON INSTANTEE_METAL_CPP_COMMIT GET "${INSTANTEE_METAL_CPP_PIN_JSON}" commit)
string(JSON INSTANTEE_METAL_CPP_SHA GET "${INSTANTEE_METAL_CPP_PIN_JSON}" archive_sha256)
if(NOT INSTANTEE_METAL_CPP_TAG STREQUAL "release/metal-cpp_macOS27.0.1_iOS27.0.1")
  message(FATAL_ERROR "Unexpected metal-cpp tag '${INSTANTEE_METAL_CPP_TAG}'")
endif()
if(NOT INSTANTEE_METAL_CPP_COMMIT STREQUAL "8cdde68ae36a636c84cbb48ebcbc8f3b60251ea0")
  message(FATAL_ERROR "Unexpected metal-cpp commit '${INSTANTEE_METAL_CPP_COMMIT}'")
endif()
if(NOT INSTANTEE_METAL_CPP_SHA STREQUAL
    "3a2f456c5373748e1b8de0896d1dbe53d6e0a1d3b8b9e5235c1a8abae6f373f5")
  message(FATAL_ERROR "Unexpected metal-cpp archive hash '${INSTANTEE_METAL_CPP_SHA}'")
endif()

include("${PROJECT_SOURCE_DIR}/cmake/CompileMetallib.cmake")

set(INSTANTEE_SHADER_MODULES_PIN
  "${PROJECT_SOURCE_DIR}/engine/shaders/modules/PIN.json")
if(NOT EXISTS "${INSTANTEE_SHADER_MODULES_PIN}")
  message(FATAL_ERROR
          "Pinned shader module manifest is missing: ${INSTANTEE_SHADER_MODULES_PIN}")
endif()
file(READ "${INSTANTEE_SHADER_MODULES_PIN}" INSTANTEE_SHADER_MODULES_PIN_JSON)
string(JSON INSTANTEE_SHADER_MODULE_COUNT LENGTH
       "${INSTANTEE_SHADER_MODULES_PIN_JSON}" modules)
math(EXPR INSTANTEE_SHADER_MODULE_LAST "${INSTANTEE_SHADER_MODULE_COUNT} - 1")
foreach(INSTANTEE_SHADER_MODULE_I RANGE 0 ${INSTANTEE_SHADER_MODULE_LAST})
  string(JSON INSTANTEE_SHADER_MODULE_ID GET
         "${INSTANTEE_SHADER_MODULES_PIN_JSON}" modules
         ${INSTANTEE_SHADER_MODULE_I} id)
  string(JSON INSTANTEE_SHADER_MODULE_SOURCE_REL GET
         "${INSTANTEE_SHADER_MODULES_PIN_JSON}" modules
         ${INSTANTEE_SHADER_MODULE_I} source)
  string(JSON INSTANTEE_SHADER_MODULE_SHA GET
         "${INSTANTEE_SHADER_MODULES_PIN_JSON}" modules
         ${INSTANTEE_SHADER_MODULE_I} source_sha256)
  get_filename_component(INSTANTEE_SHADER_MODULE_SOURCE
    "${PROJECT_SOURCE_DIR}/engine/shaders/modules/${INSTANTEE_SHADER_MODULE_SOURCE_REL}"
    ABSOLUTE)
  if(NOT EXISTS "${INSTANTEE_SHADER_MODULE_SOURCE}")
    message(FATAL_ERROR
            "Pinned shader source missing for '${INSTANTEE_SHADER_MODULE_ID}': "
            "${INSTANTEE_SHADER_MODULE_SOURCE}")
  endif()
  file(SHA256 "${INSTANTEE_SHADER_MODULE_SOURCE}" INSTANTEE_SHADER_MODULE_ACTUAL_SHA)
  if(NOT INSTANTEE_SHADER_MODULE_ACTUAL_SHA STREQUAL INSTANTEE_SHADER_MODULE_SHA)
    message(FATAL_ERROR
            "Shader source hash mismatch for '${INSTANTEE_SHADER_MODULE_ID}': "
            "expected ${INSTANTEE_SHADER_MODULE_SHA}, "
            "got ${INSTANTEE_SHADER_MODULE_ACTUAL_SHA}")
  endif()
endforeach()

set(INSTANTEE_TEST_PATTERN_SHADER
  "${PROJECT_SOURCE_DIR}/engine/shaders/spike/test_pattern.metal")
set(INSTANTEE_TEST_PATTERN_METALLIB
  "${CMAKE_CURRENT_BINARY_DIR}/generated/test_pattern.metallib")
set(INSTANTEE_TEST_PATTERN_METALLIB_HEADER
  "${CMAKE_CURRENT_BINARY_DIR}/generated/test_pattern_metallib.hpp")
instantee_compile_metallib(
  SOURCES "${INSTANTEE_TEST_PATTERN_SHADER}"
  OUTPUT_METALLIB "${INSTANTEE_TEST_PATTERN_METALLIB}"
  OUTPUT_HEADER "${INSTANTEE_TEST_PATTERN_METALLIB_HEADER}"
  SYMBOL_NAMESPACE "instantee::render_metal::detail"
  SYMBOL_NAME "kTestPatternMetallib")

set(INSTANTEE_SHARED_SURFACE_SHADER
  "${PROJECT_SOURCE_DIR}/engine/shaders/spike/shared_surface_pattern.metal")
set(INSTANTEE_SHARED_SURFACE_METALLIB
  "${CMAKE_CURRENT_BINARY_DIR}/generated/shared_surface_pattern.metallib")
set(INSTANTEE_SHARED_SURFACE_METALLIB_HEADER
  "${CMAKE_CURRENT_BINARY_DIR}/generated/shared_surface_pattern_metallib.hpp")
instantee_compile_metallib(
  SOURCES "${INSTANTEE_SHARED_SURFACE_SHADER}"
  OUTPUT_METALLIB "${INSTANTEE_SHARED_SURFACE_METALLIB}"
  OUTPUT_HEADER "${INSTANTEE_SHARED_SURFACE_METALLIB_HEADER}"
  SYMBOL_NAMESPACE "instantee::render_metal::detail"
  SYMBOL_NAME "kSharedSurfacePatternMetallib")

set(INSTANTEE_PRIMITIVES_SHADER
  "${PROJECT_SOURCE_DIR}/engine/shaders/spike/primitives.metal")
set(INSTANTEE_PRIMITIVES_METALLIB
  "${CMAKE_CURRENT_BINARY_DIR}/generated/primitives.metallib")
set(INSTANTEE_PRIMITIVES_METALLIB_HEADER
  "${CMAKE_CURRENT_BINARY_DIR}/generated/primitives_metallib.hpp")
instantee_compile_metallib(
  SOURCES "${INSTANTEE_PRIMITIVES_SHADER}"
  OUTPUT_METALLIB "${INSTANTEE_PRIMITIVES_METALLIB}"
  OUTPUT_HEADER "${INSTANTEE_PRIMITIVES_METALLIB_HEADER}"
  SYMBOL_NAMESPACE "instantee::render_metal::detail"
  SYMBOL_NAME "kPrimitivesMetallib")

set(INSTANTEE_HALFTONE_SHADER
  "${PROJECT_SOURCE_DIR}/engine/shaders/spike/halftone.metal")
set(INSTANTEE_HALFTONE_METALLIB
  "${CMAKE_CURRENT_BINARY_DIR}/generated/halftone.metallib")
set(INSTANTEE_HALFTONE_METALLIB_HEADER
  "${CMAKE_CURRENT_BINARY_DIR}/generated/halftone_metallib.hpp")
instantee_compile_metallib(
  SOURCES "${INSTANTEE_HALFTONE_SHADER}"
  OUTPUT_METALLIB "${INSTANTEE_HALFTONE_METALLIB}"
  OUTPUT_HEADER "${INSTANTEE_HALFTONE_METALLIB_HEADER}"
  SYMBOL_NAMESPACE "instantee::render_metal::detail"
  SYMBOL_NAME "kHalftoneMetallib")

set(INSTANTEE_PRINT_GRAIN_SHADER
  "${PROJECT_SOURCE_DIR}/engine/shaders/spike/print_grain.metal")
set(INSTANTEE_PRINT_GRAIN_METALLIB
  "${CMAKE_CURRENT_BINARY_DIR}/generated/print_grain.metallib")
set(INSTANTEE_PRINT_GRAIN_METALLIB_HEADER
  "${CMAKE_CURRENT_BINARY_DIR}/generated/print_grain_metallib.hpp")
instantee_compile_metallib(
  SOURCES "${INSTANTEE_PRINT_GRAIN_SHADER}"
  OUTPUT_METALLIB "${INSTANTEE_PRINT_GRAIN_METALLIB}"
  OUTPUT_HEADER "${INSTANTEE_PRINT_GRAIN_METALLIB_HEADER}"
  SYMBOL_NAMESPACE "instantee::render_metal::detail"
  SYMBOL_NAME "kPrintGrainMetallib")

set(INSTANTEE_INK_STROKE_SHADER
  "${PROJECT_SOURCE_DIR}/engine/shaders/spike/ink_stroke.metal")
set(INSTANTEE_INK_STROKE_METALLIB
  "${CMAKE_CURRENT_BINARY_DIR}/generated/ink_stroke.metallib")
set(INSTANTEE_INK_STROKE_METALLIB_HEADER
  "${CMAKE_CURRENT_BINARY_DIR}/generated/ink_stroke_metallib.hpp")
instantee_compile_metallib(
  SOURCES "${INSTANTEE_INK_STROKE_SHADER}"
  OUTPUT_METALLIB "${INSTANTEE_INK_STROKE_METALLIB}"
  OUTPUT_HEADER "${INSTANTEE_INK_STROKE_METALLIB_HEADER}"
  SYMBOL_NAMESPACE "instantee::render_metal::detail"
  SYMBOL_NAME "kInkStrokeMetallib")

find_library(INSTANTEE_METAL_FRAMEWORK Metal REQUIRED)
find_library(INSTANTEE_FOUNDATION_FRAMEWORK Foundation REQUIRED)

add_library(instantee_render_metal STATIC
  src/metal_impl.cpp
  src/device.cpp
  src/metal_backend.cpp
  src/readback.cpp
  src/shader_module.cpp
  src/test_pattern.cpp
  src/primitive_renderer.cpp
  src/clip_mask_renderer.cpp
  src/metal_glyph_atlas.cpp
  "${INSTANTEE_TEST_PATTERN_METALLIB_HEADER}"
  "${INSTANTEE_SHARED_SURFACE_METALLIB_HEADER}"
  "${INSTANTEE_PRIMITIVES_METALLIB_HEADER}"
  "${INSTANTEE_HALFTONE_METALLIB_HEADER}"
  "${INSTANTEE_PRINT_GRAIN_METALLIB_HEADER}"
  "${INSTANTEE_INK_STROKE_METALLIB_HEADER}")
add_library(instantee::render_metal ALIAS instantee_render_metal)
target_include_directories(instantee_render_metal
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
# Treat metal-cpp as a system include so its GNU layout extensions are not
# elevated by the project's pedantic warning policy.
target_include_directories(instantee_render_metal SYSTEM PRIVATE
  "${INSTANTEE_METAL_CPP_ROOT}")
target_link_libraries(instantee_render_metal
  PUBLIC instantee::diagnostics instantee::render_core instantee::typography
  PRIVATE instantee_build_options
    "${INSTANTEE_METAL_FRAMEWORK}"
    "${INSTANTEE_FOUNDATION_FRAMEWORK}")
instantee_mark_module(instantee_render_metal production render-metal)
