add_library(instantee_direction STATIC
  src/style_bible.cpp
  src/style_package_registry.cpp
  src/semantic_intent.cpp
  src/directed_film.cpp
  src/track_precedence.cpp
  src/motion_grammar.cpp
  src/composition_constraints.cpp
  src/safe_area.cpp
  src/shot_planner.cpp
  src/camera_language.cpp
  src/transition_planner.cpp
  src/creative_variation.cpp
  src/direction_report.cpp)
add_library(instantee::direction ALIAS instantee_direction)
target_include_directories(instantee_direction
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(instantee_direction
  PUBLIC instantee::diagnostics instantee::core_model
  PRIVATE instantee::yyjson instantee_build_options)
instantee_mark_module(instantee_direction production direction)
