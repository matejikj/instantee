add_executable(fuzz-asset-import-smoke main.cpp)
target_compile_definitions(fuzz-asset-import-smoke PRIVATE
  INSTANTEE_FUZZ_STANDALONE=1)
target_link_libraries(fuzz-asset-import-smoke PRIVATE
  instantee::asset_pipeline instantee::typography instantee_build_options)
set_target_properties(fuzz-asset-import-smoke PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

add_test(NAME fuzz_asset_import_smoke
  COMMAND fuzz-asset-import-smoke
          "${PROJECT_SOURCE_DIR}/engine/apps/fuzz-asset-import/corpus/svg/rect-ok.svg")
set_tests_properties(fuzz_asset_import_smoke PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_test(NAME fuzz_asset_import_corpus
  COMMAND fuzz-asset-import-smoke
          --corpus "${PROJECT_SOURCE_DIR}/engine/apps/fuzz-asset-import/corpus"
          --mutations 10)
set_tests_properties(fuzz_asset_import_corpus PROPERTIES TIMEOUT 120 LABELS
  "cpu;smoke")
