add_executable(fuzz-scene-ir-smoke main.cpp)
target_compile_definitions(fuzz-scene-ir-smoke PRIVATE INSTANTEE_FUZZ_STANDALONE=1)
target_link_libraries(fuzz-scene-ir-smoke PRIVATE instantee::scene_ir
  instantee::core_model instantee_build_options)
set_target_properties(fuzz-scene-ir-smoke PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

add_test(NAME fuzz_scene_ir_smoke
  COMMAND fuzz-scene-ir-smoke
          "${PROJECT_SOURCE_DIR}/contracts/fixtures/scene-ir/positive/minimal.scene.json")
set_tests_properties(fuzz_scene_ir_smoke PROPERTIES TIMEOUT 20 LABELS
  "cpu;smoke")

# Bounded corpus + deterministic mutations over fixtures and local seeds.
add_test(NAME fuzz_scene_ir_corpus
  COMMAND fuzz-scene-ir-smoke
          --corpus "${PROJECT_SOURCE_DIR}/engine/apps/fuzz-scene-ir/corpus"
          --fixtures "${PROJECT_SOURCE_DIR}/contracts/fixtures"
          --mutations 12)
set_tests_properties(fuzz_scene_ir_corpus PROPERTIES TIMEOUT 120 LABELS
  "cpu;smoke")
