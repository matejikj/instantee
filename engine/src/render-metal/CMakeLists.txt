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

find_library(INSTANTEE_METAL_FRAMEWORK Metal REQUIRED)
find_library(INSTANTEE_FOUNDATION_FRAMEWORK Foundation REQUIRED)

add_library(instantee_render_metal STATIC
  src/metal_impl.cpp
  src/device.cpp)
add_library(instantee::render_metal ALIAS instantee_render_metal)
target_include_directories(instantee_render_metal
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Treat metal-cpp as a system include so its GNU layout extensions are not
# elevated by the project's pedantic warning policy.
target_include_directories(instantee_render_metal SYSTEM PRIVATE
  "${INSTANTEE_METAL_CPP_ROOT}")
target_link_libraries(instantee_render_metal
  PUBLIC instantee::diagnostics
  PRIVATE instantee_build_options
    "${INSTANTEE_METAL_FRAMEWORK}"
    "${INSTANTEE_FOUNDATION_FRAMEWORK}")
