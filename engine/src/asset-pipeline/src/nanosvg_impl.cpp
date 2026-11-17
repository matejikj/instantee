// NanoSVG implementation TU. Keep private to asset-pipeline.
// Upstream nanosvg.h has a size_t vs long compare under -Wsign-compare;
// silence only for this vendored include.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
