# Declares production versus spike module roles and installs boundary checks.

set(INSTANTEE_MODULE_GRAPH "${CMAKE_SOURCE_DIR}/cmake/module_graph.json")
if(NOT EXISTS "${INSTANTEE_MODULE_GRAPH}")
  message(FATAL_ERROR "Missing module graph: ${INSTANTEE_MODULE_GRAPH}")
endif()

function(instantee_mark_module target role module_name)
  set_target_properties("${target}" PROPERTIES
    INSTANTEE_MODULE_ROLE "${role}"
    INSTANTEE_MODULE_NAME "${module_name}")
endfunction()

# One translation unit per public header so missing includes fail compilation.
function(instantee_add_isolated_public_header_tests module_target)
  get_target_property(include_dirs "${module_target}" INTERFACE_INCLUDE_DIRECTORIES)
  if(NOT include_dirs OR include_dirs STREQUAL "include_dirs-NOTFOUND")
    return()
  endif()
  unset(header_root)
  foreach(dir IN LISTS include_dirs)
    if(EXISTS "${dir}/instantee")
      set(header_root "${dir}")
      break()
    endif()
  endforeach()
  if(NOT header_root)
    return()
  endif()

  file(GLOB_RECURSE public_headers CONFIGURE_DEPENDS
    "${header_root}/instantee/*.hpp")
  if(NOT public_headers)
    return()
  endif()

  set(gen_dir "${CMAKE_CURRENT_BINARY_DIR}/isolated_headers/${module_target}")
  file(MAKE_DIRECTORY "${gen_dir}")
  foreach(header IN LISTS public_headers)
    file(RELATIVE_PATH rel "${header_root}" "${header}")
    string(REPLACE "/" "_" safe "${rel}")
    string(REPLACE ".hpp" "" safe "${safe}")
    set(tu "${gen_dir}/${safe}.cpp")
    file(WRITE "${tu}" "#include <${rel}>\n")
    set(test_target "${module_target}_header_${safe}")
    add_library("${test_target}" OBJECT "${tu}")
    target_link_libraries("${test_target}" PRIVATE "${module_target}"
      instantee_build_options)
    add_test(NAME "header_${module_target}_${safe}"
      COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
              --target "${test_target}")
    set_tests_properties("header_${module_target}_${safe}" PROPERTIES
      TIMEOUT 60 LABELS "cpu;headers")
  endforeach()
endfunction()
