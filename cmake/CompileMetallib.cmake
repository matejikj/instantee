# Compile one or more .metal sources into a pinned .metallib and embed the
# binary as a C++ header byte array. Requires the Apple Metal toolchain
# (`xcrun metal` / `xcrun metallib`).

function(instantee_embed_binary_as_header input_file output_header
                                          symbol_namespace symbol_name)
  file(READ "${input_file}" _instantee_bin_hex HEX)
  if(_instantee_bin_hex STREQUAL "")
    message(FATAL_ERROR "Cannot embed empty binary: ${input_file}")
  endif()
  set(_instantee_bin_list "")
  string(LENGTH "${_instantee_bin_hex}" _instantee_bin_len)
  math(EXPR _instantee_bin_last "${_instantee_bin_len} - 1")
  foreach(_instantee_bin_i RANGE 0 ${_instantee_bin_last} 2)
    string(SUBSTRING "${_instantee_bin_hex}" ${_instantee_bin_i} 2
           _instantee_bin_byte)
    string(APPEND _instantee_bin_list
           "static_cast<unsigned char>(0x${_instantee_bin_byte}),")
  endforeach()
  file(WRITE "${output_header}"
"#pragma once

#include <cstddef>

namespace ${symbol_namespace} {

inline constexpr unsigned char ${symbol_name}[] = {${_instantee_bin_list}};
inline constexpr std::size_t ${symbol_name}_size = sizeof(${symbol_name});

} // namespace ${symbol_namespace}
")
endfunction()

function(instantee_compile_metallib)
  set(options)
  set(one_value_args OUTPUT_METALLIB OUTPUT_HEADER SYMBOL_NAMESPACE SYMBOL_NAME)
  set(multi_value_args SOURCES)
  cmake_parse_arguments(IMM "${options}" "${one_value_args}"
                        "${multi_value_args}" ${ARGN})
  if(NOT IMM_SOURCES OR NOT IMM_OUTPUT_METALLIB OR NOT IMM_OUTPUT_HEADER OR
     NOT IMM_SYMBOL_NAMESPACE OR NOT IMM_SYMBOL_NAME)
    message(FATAL_ERROR
            "instantee_compile_metallib requires SOURCES OUTPUT_METALLIB "
            "OUTPUT_HEADER SYMBOL_NAMESPACE SYMBOL_NAME")
  endif()

  find_program(INSTANTEE_XCRUN xcrun REQUIRED)
  set(_air_files "")
  set(_index 0)
  foreach(_src IN LISTS IMM_SOURCES)
    get_filename_component(_src_abs "${_src}" ABSOLUTE)
    set(_air "${CMAKE_CURRENT_BINARY_DIR}/metallib/${IMM_SYMBOL_NAME}_${_index}.air")
    add_custom_command(
      OUTPUT "${_air}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory
              "${CMAKE_CURRENT_BINARY_DIR}/metallib"
      COMMAND "${INSTANTEE_XCRUN}" -sdk macosx metal -c "${_src_abs}" -o "${_air}"
      DEPENDS "${_src_abs}"
      COMMENT "Compiling Metal shader ${_src_abs}"
      VERBATIM)
    list(APPEND _air_files "${_air}")
    math(EXPR _index "${_index} + 1")
  endforeach()

  add_custom_command(
    OUTPUT "${IMM_OUTPUT_METALLIB}"
    COMMAND "${INSTANTEE_XCRUN}" -sdk macosx metallib ${_air_files}
            -o "${IMM_OUTPUT_METALLIB}"
    DEPENDS ${_air_files}
    COMMENT "Linking metallib ${IMM_OUTPUT_METALLIB}"
    VERBATIM)

  set(_embed_script
      "${CMAKE_CURRENT_BINARY_DIR}/metallib/embed_${IMM_SYMBOL_NAME}.cmake")
  file(WRITE "${_embed_script}"
"include(\"${PROJECT_SOURCE_DIR}/cmake/CompileMetallib.cmake\")
instantee_embed_binary_as_header(
  \"${IMM_OUTPUT_METALLIB}\"
  \"${IMM_OUTPUT_HEADER}\"
  \"${IMM_SYMBOL_NAMESPACE}\"
  \"${IMM_SYMBOL_NAME}\")
")
  add_custom_command(
    OUTPUT "${IMM_OUTPUT_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -P "${_embed_script}"
    DEPENDS "${IMM_OUTPUT_METALLIB}"
            "${PROJECT_SOURCE_DIR}/cmake/CompileMetallib.cmake"
    COMMENT "Embedding metallib ${IMM_SYMBOL_NAME}"
    VERBATIM)
endfunction()
