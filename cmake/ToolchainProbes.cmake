set(INSTANTEE_TOOLCHAIN_PROBE_TARGETS)
foreach(language IN ITEMS CXX OBJCXX)
  if(language STREQUAL "CXX")
    set(extension cpp)
  else()
    set(extension mm)
  endif()
  foreach(feature IN ITEMS expected jthread stop_token stop_wait)
    set(name "probe_${language}_${feature}")
    set(source "${PROJECT_SOURCE_DIR}/cmake/probes/${feature}.cpp")
    try_compile(compiled
      SOURCE_FROM_FILE "${name}.${extension}" "${source}"
      NO_CACHE
      CMAKE_FLAGS "-DINCLUDE_DIRECTORIES:STRING=${PROJECT_SOURCE_DIR}/cmake/probes"
      COMPILE_DEFINITIONS -Wall -Wextra -Werror -pedantic-errors
      COPY_FILE "${PROJECT_BINARY_DIR}/${name}-configure"
      OUTPUT_VARIABLE diagnostics)
    if(NOT compiled)
      message(FATAL_ERROR "Required ${language} feature ${feature} failed to compile:\n${diagnostics}")
    endif()
    execute_process(COMMAND "${PROJECT_BINARY_DIR}/${name}-configure"
      RESULT_VARIABLE result TIMEOUT 10)
    if(NOT result STREQUAL "0")
      message(FATAL_ERROR "Required ${language} feature ${feature} failed to run: ${result}")
    endif()
    message(STATUS "Required ${language} feature ${feature}: compile/run passed")
    if(language STREQUAL "OBJCXX")
      set(source "${PROJECT_BINARY_DIR}/${name}.mm")
      file(WRITE "${source}" "#include \"${PROJECT_SOURCE_DIR}/cmake/probes/${feature}.cpp\"\n")
    endif()
    add_executable("${name}" "${source}")
    list(APPEND INSTANTEE_TOOLCHAIN_PROBE_TARGETS "${name}")
    target_link_libraries("${name}" PRIVATE instantee_build_options)
    add_test(NAME "${name}" COMMAND "${name}")
    set_tests_properties("${name}" PROPERTIES TIMEOUT 10 LABELS cpu)
  endforeach()
endforeach()
