find_library(INSTANTEE_FOUNDATION Foundation REQUIRED)
find_library(INSTANTEE_CORE_FOUNDATION CoreFoundation REQUIRED)

add_library(instantee_export_apple STATIC src/workspace.mm)
add_library(instantee::export_apple ALIAS instantee_export_apple)
target_include_directories(instantee_export_apple PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(instantee_export_apple PRIVATE -fobjc-arc)
target_link_libraries(instantee_export_apple PUBLIC instantee::diagnostics
  PRIVATE instantee_build_options "${INSTANTEE_FOUNDATION}" "${INSTANTEE_CORE_FOUNDATION}")
