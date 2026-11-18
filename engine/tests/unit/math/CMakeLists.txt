add_executable(spike_math_grammar_test math_grammar_test.cpp)
target_link_libraries(spike_math_grammar_test PRIVATE
  instantee::math instantee_build_options)
add_test(NAME spike_math_grammar
  COMMAND spike_math_grammar_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_math_grammar PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")
