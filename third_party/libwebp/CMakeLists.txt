# Decode-only libwebp. Keep PRIVATE to asset-pipeline; do not re-export headers.
set(INSTANTEE_LIBWEBP_DEC_SOURCES
  src/dec/alpha_dec.c
  src/dec/buffer_dec.c
  src/dec/frame_dec.c
  src/dec/idec_dec.c
  src/dec/io_dec.c
  src/dec/quant_dec.c
  src/dec/tree_dec.c
  src/dec/vp8_dec.c
  src/dec/vp8l_dec.c
  src/dec/webp_dec.c)

file(GLOB INSTANTEE_LIBWEBP_DSP_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/*.c")
list(FILTER INSTANTEE_LIBWEBP_DSP_SOURCES EXCLUDE REGEX ".*/(enc|cost|ssim).*\\.c$")
list(FILTER INSTANTEE_LIBWEBP_DSP_SOURCES EXCLUDE REGEX ".*/lossless_enc.*\\.c$")

set(INSTANTEE_LIBWEBP_UTILS_SOURCES
  src/utils/bit_reader_utils.c
  src/utils/color_cache_utils.c
  src/utils/filters_utils.c
  src/utils/huffman_utils.c
  src/utils/palette.c
  src/utils/quant_levels_dec_utils.c
  src/utils/random_utils.c
  src/utils/rescaler_utils.c
  src/utils/thread_utils.c
  src/utils/utils.c)

add_library(instantee_libwebp_decode STATIC
  ${INSTANTEE_LIBWEBP_DEC_SOURCES}
  ${INSTANTEE_LIBWEBP_DSP_SOURCES}
  ${INSTANTEE_LIBWEBP_UTILS_SOURCES})
add_library(instantee::libwebp_decode ALIAS instantee_libwebp_decode)
# Upstream sources include headers as "src/dec/..." and "src/webp/...".
target_include_directories(instantee_libwebp_decode
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(instantee_libwebp_decode
  PRIVATE WEBP_USE_THREAD=0)
set_target_properties(instantee_libwebp_decode PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
# Upstream relies on assert() in Debug; Release uses NDEBUG from the toolchain.
