# Pinned libtess2. Keep PRIVATE to geometry; do not re-export tesselator.h.
set(INSTANTEE_LIBTESS2_SOURCES
  Source/bucketalloc.c
  Source/dict.c
  Source/geom.c
  Source/mesh.c
  Source/priorityq.c
  Source/sweep.c
  Source/tess.c)

add_library(instantee_libtess2 STATIC ${INSTANTEE_LIBTESS2_SOURCES})
add_library(instantee::libtess2 ALIAS instantee_libtess2)
target_include_directories(instantee_libtess2
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
set_target_properties(instantee_libtess2 PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
# Upstream uses assert(); keep them enabled only in Debug builds via NDEBUG.
