add_library(instantee_export_core STATIC
  src/write_all.cpp
  src/output_transaction.cpp
  src/export_recovery.cpp
  src/video_sink.cpp
  src/frame_bridge.cpp
  src/rec709_handoff.cpp)
add_library(instantee::export_core ALIAS instantee_export_core)
target_include_directories(instantee_export_core
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_export_core
  PUBLIC instantee::diagnostics instantee::core_model
  PRIVATE instantee_build_options)
instantee_mark_module(instantee_export_core production export-core)
