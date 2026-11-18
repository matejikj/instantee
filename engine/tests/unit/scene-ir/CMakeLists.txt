add_executable(spike_scene_ir_load_test load_test.cpp)
target_link_libraries(spike_scene_ir_load_test PRIVATE instantee::scene_ir
  instantee_build_options)
target_compile_definitions(spike_scene_ir_load_test PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_scene_ir_load COMMAND spike_scene_ir_load_test)
set_tests_properties(spike_scene_ir_load PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_scene_ir_graph_test graph_test.cpp)
target_link_libraries(spike_scene_ir_graph_test PRIVATE instantee::scene_ir
  instantee_build_options)
target_compile_definitions(spike_scene_ir_graph_test PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_scene_ir_graph COMMAND spike_scene_ir_graph_test)
set_tests_properties(spike_scene_ir_graph PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_scene_ir_graph_edit_test graph_edit_test.cpp)
target_link_libraries(spike_scene_ir_graph_edit_test PRIVATE instantee::scene_ir
  instantee_build_options)
add_test(NAME spike_scene_ir_graph_edit COMMAND spike_scene_ir_graph_edit_test)
set_tests_properties(spike_scene_ir_graph_edit PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_scene_ir_security_test security_test.cpp)
target_link_libraries(spike_scene_ir_security_test PRIVATE instantee::scene_ir
  instantee_build_options)
target_compile_definitions(spike_scene_ir_security_test PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_scene_ir_security COMMAND spike_scene_ir_security_test)
set_tests_properties(spike_scene_ir_security PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_scene_ir_canonicalize_test canonicalize_test.cpp)
target_link_libraries(spike_scene_ir_canonicalize_test PRIVATE
  instantee::scene_ir instantee_build_options)
target_compile_definitions(spike_scene_ir_canonicalize_test PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_scene_ir_canonicalize COMMAND spike_scene_ir_canonicalize_test)
set_tests_properties(spike_scene_ir_canonicalize PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_scene_ir_lockfile_test lockfile_test.cpp)
target_link_libraries(spike_scene_ir_lockfile_test PRIVATE instantee::scene_ir
  instantee_build_options)
target_compile_definitions(spike_scene_ir_lockfile_test PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_scene_ir_lockfile COMMAND spike_scene_ir_lockfile_test)
set_tests_properties(spike_scene_ir_lockfile PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_scene_ir_editorial_asset_lock_test
  editorial_asset_lock_test.cpp)
target_link_libraries(spike_scene_ir_editorial_asset_lock_test PRIVATE
  instantee::scene_ir instantee_build_options)
add_test(NAME spike_scene_ir_editorial_asset_lock
  COMMAND spike_scene_ir_editorial_asset_lock_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_scene_ir_editorial_asset_lock PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_scene_ir_notebook_asset_lock_test
  notebook_asset_lock_test.cpp)
target_link_libraries(spike_scene_ir_notebook_asset_lock_test PRIVATE
  instantee::scene_ir instantee_build_options)
add_test(NAME spike_scene_ir_notebook_asset_lock
  COMMAND spike_scene_ir_notebook_asset_lock_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_scene_ir_notebook_asset_lock PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_scene_ir_migrate_test migrate_test.cpp)
target_link_libraries(spike_scene_ir_migrate_test PRIVATE instantee::scene_ir
  instantee_build_options)
target_compile_definitions(spike_scene_ir_migrate_test PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_scene_ir_migrate COMMAND spike_scene_ir_migrate_test)
set_tests_properties(spike_scene_ir_migrate PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_scene_ir_semantic_fixture_test semantic_fixture_test.cpp)
target_link_libraries(spike_scene_ir_semantic_fixture_test PRIVATE
  instantee::scene_ir instantee_build_options)
target_compile_definitions(spike_scene_ir_semantic_fixture_test PRIVATE
  "INSTANTEE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME spike_scene_ir_semantic_fixture
  COMMAND spike_scene_ir_semantic_fixture_test)
set_tests_properties(spike_scene_ir_semantic_fixture PROPERTIES TIMEOUT 30
  LABELS "cpu;smoke")

add_executable(spike_scene_ir_puppet_rig_test puppet_rig_test.cpp)
target_link_libraries(spike_scene_ir_puppet_rig_test PRIVATE
  instantee::scene_ir instantee_build_options)
add_test(NAME spike_scene_ir_puppet_rig
  COMMAND spike_scene_ir_puppet_rig_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_scene_ir_puppet_rig PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_scene_ir_puppet_fk_test puppet_fk_test.cpp)
target_link_libraries(spike_scene_ir_puppet_fk_test PRIVATE
  instantee::scene_ir instantee_build_options)
add_test(NAME spike_scene_ir_puppet_fk COMMAND spike_scene_ir_puppet_fk_test)
set_tests_properties(spike_scene_ir_puppet_fk PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_scene_ir_puppet_ik_test puppet_ik_test.cpp)
target_link_libraries(spike_scene_ir_puppet_ik_test PRIVATE
  instantee::scene_ir instantee_build_options)
add_test(NAME spike_scene_ir_puppet_ik COMMAND spike_scene_ir_puppet_ik_test)
set_tests_properties(spike_scene_ir_puppet_ik PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")

add_executable(spike_scene_ir_puppet_skin_test puppet_skin_test.cpp)
target_link_libraries(spike_scene_ir_puppet_skin_test PRIVATE
  instantee::scene_ir instantee_build_options)
add_test(NAME spike_scene_ir_puppet_skin
  COMMAND spike_scene_ir_puppet_skin_test "${PROJECT_SOURCE_DIR}")
set_tests_properties(spike_scene_ir_puppet_skin PROPERTIES TIMEOUT 30 LABELS
  "cpu;smoke")
