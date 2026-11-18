add_executable(spike_math_grammar_test math_grammar_test.cpp)
target_link_libraries(spike_math_grammar_test PRIVATE
  instantee::math instantee_build_options)
add_test(NAME spike_math_grammar
  COMMAND spike_math_grammar_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_math_grammar PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_math_parser_test math_parser_test.cpp)
target_link_libraries(spike_math_parser_test PRIVATE
  instantee::math instantee_build_options)
add_test(NAME spike_math_parser COMMAND spike_math_parser_test)
set_tests_properties(spike_math_parser PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_math_layout_test math_layout_test.cpp)
target_link_libraries(spike_math_layout_test PRIVATE
  instantee::math instantee_build_options)
add_test(NAME spike_math_layout
  COMMAND spike_math_layout_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_math_layout PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_math_semantic_test math_semantic_test.cpp)
target_link_libraries(spike_math_semantic_test PRIVATE
  instantee::math instantee_build_options)
add_test(NAME spike_math_semantic COMMAND spike_math_semantic_test)
set_tests_properties(spike_math_semantic PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_math_morph_test math_morph_test.cpp)
target_link_libraries(spike_math_morph_test PRIVATE
  instantee::math instantee_build_options)
add_test(NAME spike_math_morph
  COMMAND spike_math_morph_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_math_morph PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")
