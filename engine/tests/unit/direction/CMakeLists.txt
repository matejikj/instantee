add_executable(spike_direction_style_bible_test style_bible_test.cpp)
target_link_libraries(spike_direction_style_bible_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_style_bible
  COMMAND spike_direction_style_bible_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_direction_style_bible PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_direction_style_package_registry_test
  style_package_registry_test.cpp)
target_link_libraries(spike_direction_style_package_registry_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_style_package_registry
  COMMAND spike_direction_style_package_registry_test)
set_tests_properties(spike_direction_style_package_registry PROPERTIES
  TIMEOUT 30 LABELS "cpu;smoke")

add_executable(spike_direction_semantic_intent_test semantic_intent_test.cpp)
target_link_libraries(spike_direction_semantic_intent_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_semantic_intent
  COMMAND spike_direction_semantic_intent_test)
set_tests_properties(spike_direction_semantic_intent PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_direction_track_precedence_test track_precedence_test.cpp)
target_link_libraries(spike_direction_track_precedence_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_track_precedence
  COMMAND spike_direction_track_precedence_test)
set_tests_properties(spike_direction_track_precedence PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_direction_motion_grammar_test motion_grammar_test.cpp)
target_link_libraries(spike_direction_motion_grammar_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_motion_grammar
  COMMAND spike_direction_motion_grammar_test)
set_tests_properties(spike_direction_motion_grammar PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_direction_composition_constraints_test
  composition_constraints_test.cpp)
target_link_libraries(spike_direction_composition_constraints_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_composition_constraints
  COMMAND spike_direction_composition_constraints_test)
set_tests_properties(spike_direction_composition_constraints PROPERTIES
  TIMEOUT 30 LABELS "cpu;smoke")

add_executable(spike_direction_composition_typography_metrics_test
  composition_typography_metrics_test.cpp)
target_link_libraries(spike_direction_composition_typography_metrics_test
  PRIVATE instantee::direction instantee::typography instantee_build_options)
target_compile_definitions(
  spike_direction_composition_typography_metrics_test PRIVATE
  "INSTANTEE_PINNED_FONT_DIR=\"${PROJECT_SOURCE_DIR}/third_party/fonts\"")
add_test(NAME spike_direction_composition_typography_metrics
  COMMAND spike_direction_composition_typography_metrics_test)
set_tests_properties(spike_direction_composition_typography_metrics PROPERTIES
  TIMEOUT 60 LABELS "cpu;smoke")

add_executable(spike_direction_safe_area_test safe_area_test.cpp)
target_link_libraries(spike_direction_safe_area_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_safe_area
  COMMAND spike_direction_safe_area_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_direction_safe_area PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_direction_shot_planner_test shot_planner_test.cpp)
target_link_libraries(spike_direction_shot_planner_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_shot_planner
  COMMAND spike_direction_shot_planner_test)
set_tests_properties(spike_direction_shot_planner PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_direction_camera_language_test camera_language_test.cpp)
target_link_libraries(spike_direction_camera_language_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_camera_language
  COMMAND spike_direction_camera_language_test)
set_tests_properties(spike_direction_camera_language PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_direction_transition_planner_test
  transition_planner_test.cpp)
target_link_libraries(spike_direction_transition_planner_test PRIVATE
  instantee::direction instantee::runtime instantee_build_options)
add_test(NAME spike_direction_transition_planner
  COMMAND spike_direction_transition_planner_test)
set_tests_properties(spike_direction_transition_planner PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_direction_editorial_transitions_test
  editorial_transitions_test.cpp)
target_link_libraries(spike_direction_editorial_transitions_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_editorial_transitions
  COMMAND spike_direction_editorial_transitions_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_direction_editorial_transitions PROPERTIES
  TIMEOUT 30 LABELS "cpu;smoke")

add_executable(spike_direction_editorial_beats_test editorial_beats_test.cpp)
target_link_libraries(spike_direction_editorial_beats_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_editorial_beats
  COMMAND spike_direction_editorial_beats_test)
set_tests_properties(spike_direction_editorial_beats PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_direction_creative_variation_test
  creative_variation_test.cpp)
target_link_libraries(spike_direction_creative_variation_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_creative_variation
  COMMAND spike_direction_creative_variation_test)
set_tests_properties(spike_direction_creative_variation PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_direction_direction_report_test direction_report_test.cpp)
target_link_libraries(spike_direction_direction_report_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_direction_report
  COMMAND spike_direction_direction_report_test)
set_tests_properties(spike_direction_direction_report PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_direction_typography_lint_test typography_lint_test.cpp)
target_link_libraries(spike_direction_typography_lint_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_typography_lint
  COMMAND spike_direction_typography_lint_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_direction_typography_lint PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_direction_layout_lint_test layout_lint_test.cpp)
target_link_libraries(spike_direction_layout_lint_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_layout_lint
  COMMAND spike_direction_layout_lint_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_direction_layout_lint PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_direction_motion_palette_lint_test
  motion_palette_lint_test.cpp)
target_link_libraries(spike_direction_motion_palette_lint_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_motion_palette_lint
  COMMAND spike_direction_motion_palette_lint_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_direction_motion_palette_lint PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_direction_lint_policy_test lint_policy_test.cpp)
target_link_libraries(spike_direction_lint_policy_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_lint_policy
  COMMAND spike_direction_lint_policy_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_direction_lint_policy PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_direction_editorial_style_review_test
  editorial_style_review_test.cpp)
target_link_libraries(spike_direction_editorial_style_review_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_editorial_style_review
  COMMAND spike_direction_editorial_style_review_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_direction_editorial_style_review PROPERTIES
  TIMEOUT 30 LABELS "cpu;smoke")

add_executable(spike_direction_headline_reveal_test headline_reveal_test.cpp)
target_link_libraries(spike_direction_headline_reveal_test PRIVATE
  instantee::direction instantee::typography instantee_build_options)
add_test(NAME spike_direction_headline_reveal
  COMMAND spike_direction_headline_reveal_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_direction_headline_reveal PROPERTIES TIMEOUT 60
  LABELS "cpu;smoke")

add_executable(spike_direction_editorial_asym_layout_test
  editorial_asym_layout_test.cpp)
target_link_libraries(spike_direction_editorial_asym_layout_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_editorial_asym_layout
  COMMAND spike_direction_editorial_asym_layout_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_direction_editorial_asym_layout PROPERTIES
  TIMEOUT 30 LABELS "cpu;smoke")

add_executable(spike_direction_editorial_reels_composition_test
  editorial_reels_composition_test.cpp)
target_link_libraries(spike_direction_editorial_reels_composition_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_editorial_reels_composition
  COMMAND spike_direction_editorial_reels_composition_test
          "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_direction_editorial_reels_composition PROPERTIES
  TIMEOUT 30 LABELS "cpu;smoke")

add_executable(spike_direction_news_scene_templates_test
  news_scene_templates_test.cpp)
target_link_libraries(spike_direction_news_scene_templates_test PRIVATE
  instantee::direction instantee_build_options)
add_test(NAME spike_direction_news_scene_templates
  COMMAND spike_direction_news_scene_templates_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_direction_news_scene_templates PROPERTIES
  TIMEOUT 30 LABELS "cpu;smoke")

add_executable(spike_direction_editorial_visual_acceptance_test
  editorial_visual_acceptance_test.cpp)
target_link_libraries(spike_direction_editorial_visual_acceptance_test PRIVATE
  instantee::direction instantee::compiler instantee::scene_ir
  instantee::preview instantee::runtime instantee_build_options)
add_test(NAME spike_direction_editorial_visual_acceptance
  COMMAND spike_direction_editorial_visual_acceptance_test
          "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_direction_editorial_visual_acceptance PROPERTIES
  TIMEOUT 120 LABELS "cpu;smoke")

# CLI contract: release-mode strict rejection and waived audit path via the
# production `instantee lint` entrypoint (binary built by the release preset).
add_test(
  NAME instantee_lint_policy_cli_reject
  COMMAND
    "${PROJECT_BINARY_DIR}/instantee" lint
    "${PROJECT_SOURCE_DIR}/contracts/fixtures/lint-policy/findings-with-error.json"
    --policy strict)
set_tests_properties(instantee_lint_policy_cli_reject PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke" WILL_FAIL TRUE)

add_test(
  NAME instantee_lint_policy_cli_waiver
  COMMAND
    "${PROJECT_BINARY_DIR}/instantee" lint
    "${PROJECT_SOURCE_DIR}/contracts/fixtures/lint-policy/findings-with-error.json"
    --policy strict
    --waivers
    "${PROJECT_SOURCE_DIR}/contracts/fixtures/lint-policy/waiver-hard-overlap.json")
set_tests_properties(instantee_lint_policy_cli_waiver PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")
