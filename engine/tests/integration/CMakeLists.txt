find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME spike_cli_smoke COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/cli_smoke.py"
  "$<TARGET_FILE:spike-render>" "$<TARGET_FILE:spike-benchmark>")
set_tests_properties(spike_cli_smoke PROPERTIES TIMEOUT 30 LABELS smoke)

add_executable(spike_cancellation_child cancellation_child.mm)
target_compile_options(spike_cancellation_child PRIVATE -fobjc-arc)
target_link_libraries(spike_cancellation_child PRIVATE instantee::runtime
  instantee::export_core instantee::export_apple instantee::render_metal
  instantee_build_options)
add_test(NAME spike_cancellation_subprocess COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/cancellation_subprocess.py"
  "$<TARGET_FILE:spike_cancellation_child>"
  "${PROJECT_SOURCE_DIR}/tools/workspace/supervise.py")
set_tests_properties(spike_cancellation_subprocess PROPERTIES TIMEOUT 180
  LABELS "cpu;smoke")

add_test(NAME spike_measurement COMMAND "${Python3_EXECUTABLE}"
  "${PROJECT_SOURCE_DIR}/tools/benchmark/test_measure.py"
  "$<TARGET_FILE:spike-export-metrics>")
set_tests_properties(spike_measurement PROPERTIES TIMEOUT 120 LABELS "cpu;smoke")

add_executable(instantee_cli_validate_compile_test cli_validate_compile_test.cpp)
target_link_libraries(instantee_cli_validate_compile_test PRIVATE
  instantee_build_options)
add_dependencies(instantee_cli_validate_compile_test instantee)
add_test(NAME instantee_cli_validate_compile
  COMMAND instantee_cli_validate_compile_test
  "${PROJECT_SOURCE_DIR}" "$<TARGET_FILE:instantee>")
set_tests_properties(instantee_cli_validate_compile PROPERTIES TIMEOUT 60
  LABELS "cpu;smoke")

add_executable(instantee_frame_seek_test frame_seek_test.cpp)
target_link_libraries(instantee_frame_seek_test PRIVATE instantee_build_options)
add_dependencies(instantee_frame_seek_test instantee)
add_test(NAME instantee_frame_seek
  COMMAND instantee_frame_seek_test
  "${PROJECT_SOURCE_DIR}" "$<TARGET_FILE:instantee>")
set_tests_properties(instantee_frame_seek PROPERTIES TIMEOUT 60 LABELS
  "cpu;smoke")

find_library(INSTANTEE_COMPRESSION_FRAMEWORK Compression REQUIRED)
add_executable(instantee_walking_skeleton_test walking_skeleton_test.cpp)
target_link_libraries(instantee_walking_skeleton_test PRIVATE
  instantee::runtime instantee::render_core instantee::export_core
  instantee_build_options "${INSTANTEE_COMPRESSION_FRAMEWORK}")
add_test(NAME instantee_walking_skeleton
  COMMAND instantee_walking_skeleton_test
  "${CMAKE_BINARY_DIR}/walking-skeleton-out")
set_tests_properties(instantee_walking_skeleton PROPERTIES TIMEOUT 60 LABELS
  "cpu;smoke")

add_executable(instantee_scene_video_mp4_test scene_video_mp4_test.cpp)
target_link_libraries(instantee_scene_video_mp4_test PRIVATE
  instantee::export instantee::export_apple instantee::compiler
  instantee::scene_ir instantee_build_options)
add_test(NAME instantee_scene_video_mp4
  COMMAND instantee_scene_video_mp4_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(instantee_scene_video_mp4 PROPERTIES TIMEOUT 120 LABELS
  "cpu;smoke;pts")

add_executable(instantee_cds_sdk_e2e_test cds_sdk_e2e_test.cpp)
target_link_libraries(instantee_cds_sdk_e2e_test PRIVATE
  instantee::export instantee::export_apple instantee::compiler
  instantee::scene_ir instantee_build_options)
add_test(NAME instantee_cds_sdk_e2e
  COMMAND instantee_cds_sdk_e2e_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(instantee_cds_sdk_e2e PROPERTIES TIMEOUT 120 LABELS
  "cpu;smoke;pts")

add_executable(instantee_p3_gate_metrics_smoke_test
  p3_gate_metrics_smoke_test.cpp)
target_link_libraries(instantee_p3_gate_metrics_smoke_test PRIVATE
  instantee_build_options)
add_dependencies(instantee_p3_gate_metrics_smoke_test p3-gate-metrics)
add_test(NAME instantee_p3_gate_metrics_smoke
  COMMAND instantee_p3_gate_metrics_smoke_test
  "$<TARGET_FILE:p3-gate-metrics>")
set_tests_properties(instantee_p3_gate_metrics_smoke PROPERTIES TIMEOUT 120
  LABELS "cpu;smoke;pts")
