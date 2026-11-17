add_executable(spike_runtime_backpressure_test backpressure_test.cpp)
target_link_libraries(spike_runtime_backpressure_test PRIVATE instantee::runtime
  instantee_build_options)
add_test(NAME spike_runtime_backpressure COMMAND spike_runtime_backpressure_test)
set_tests_properties(spike_runtime_backpressure PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_runtime_cancellation_test cancellation_test.cpp)
target_link_libraries(spike_runtime_cancellation_test PRIVATE instantee::runtime
  instantee_build_options)
add_test(NAME spike_runtime_cancellation COMMAND spike_runtime_cancellation_test)
set_tests_properties(spike_runtime_cancellation PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_frame_samples_test frame_samples_test.cpp)
target_link_libraries(spike_frame_samples_test PRIVATE instantee::runtime instantee_build_options)
add_test(NAME spike_frame_samples COMMAND spike_frame_samples_test)
set_tests_properties(spike_frame_samples PROPERTIES TIMEOUT 10 LABELS cpu)

add_executable(spike_runtime_compiled_scene_test compiled_scene_test.cpp)
target_link_libraries(spike_runtime_compiled_scene_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_compiled_scene
  COMMAND spike_runtime_compiled_scene_test)
set_tests_properties(spike_runtime_compiled_scene PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_runtime_transforms_test transforms_test.cpp)
target_link_libraries(spike_runtime_transforms_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_transforms COMMAND spike_runtime_transforms_test)
set_tests_properties(spike_runtime_transforms PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_runtime_layering_test layering_test.cpp)
target_link_libraries(spike_runtime_layering_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_layering COMMAND spike_runtime_layering_test)
set_tests_properties(spike_runtime_layering PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_runtime_timeline_test timeline_test.cpp)
target_link_libraries(spike_runtime_timeline_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_timeline COMMAND spike_runtime_timeline_test)
set_tests_properties(spike_runtime_timeline PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_runtime_easing_test easing_test.cpp)
target_link_libraries(spike_runtime_easing_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_easing COMMAND spike_runtime_easing_test)
set_tests_properties(spike_runtime_easing PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_runtime_spring_test spring_test.cpp)
target_link_libraries(spike_runtime_spring_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_spring COMMAND spike_runtime_spring_test)
set_tests_properties(spike_runtime_spring PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_runtime_frame_evaluator_test frame_evaluator_test.cpp)
target_link_libraries(spike_runtime_frame_evaluator_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_frame_evaluator
  COMMAND spike_runtime_frame_evaluator_test)
set_tests_properties(spike_runtime_frame_evaluator PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_runtime_shot_timeline_test shot_timeline_test.cpp)
target_link_libraries(spike_runtime_shot_timeline_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_shot_timeline
  COMMAND spike_runtime_shot_timeline_test)
set_tests_properties(spike_runtime_shot_timeline PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_runtime_transition_schedule_test
  transition_schedule_test.cpp)
target_link_libraries(spike_runtime_transition_schedule_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_transition_schedule
  COMMAND spike_runtime_transition_schedule_test)
set_tests_properties(spike_runtime_transition_schedule PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_runtime_clip_mask_test clip_mask_test.cpp)
target_link_libraries(spike_runtime_clip_mask_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_clip_mask COMMAND spike_runtime_clip_mask_test)
set_tests_properties(spike_runtime_clip_mask PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_runtime_frame_preview_test frame_preview_test.cpp)
target_link_libraries(spike_runtime_frame_preview_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_frame_preview
  COMMAND spike_runtime_frame_preview_test)
set_tests_properties(spike_runtime_frame_preview PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_runtime_frame_eval_scheduler_test
  frame_eval_scheduler_test.cpp)
target_link_libraries(spike_runtime_frame_eval_scheduler_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_frame_eval_scheduler
  COMMAND spike_runtime_frame_eval_scheduler_test)
set_tests_properties(spike_runtime_frame_eval_scheduler PROPERTIES TIMEOUT 60
  LABELS "cpu;smoke")

add_executable(spike_runtime_determinism_phase_gate_test
  determinism_phase_gate_test.cpp)
target_link_libraries(spike_runtime_determinism_phase_gate_test PRIVATE
  instantee::runtime instantee_build_options)
add_test(NAME spike_runtime_determinism_phase_gate
  COMMAND spike_runtime_determinism_phase_gate_test)
set_tests_properties(spike_runtime_determinism_phase_gate PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")
