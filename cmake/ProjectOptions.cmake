set(INSTANTEE_SANITIZER "none" CACHE STRING "Diagnostic instrumentation")
set_property(CACHE INSTANTEE_SANITIZER PROPERTY STRINGS none address-undefined thread)
add_library(instantee_build_options INTERFACE)
# Deterministic logical-state builds (SAS §7.1): no fast-math; disable FP
# contraction so the compiler does not silently form FMAs that change rounding.
target_compile_options(instantee_build_options INTERFACE
  -Wall -Wextra -Werror -pedantic-errors
  -fno-fast-math
  -ffp-contract=off)
if(INSTANTEE_SANITIZER STREQUAL "address-undefined")
  set(sanitizer_flags -fsanitize=address,undefined -fno-omit-frame-pointer)
elseif(INSTANTEE_SANITIZER STREQUAL "thread")
  set(sanitizer_flags -fsanitize=thread -fno-omit-frame-pointer)
elseif(NOT INSTANTEE_SANITIZER STREQUAL "none")
  message(FATAL_ERROR "Unsupported INSTANTEE_SANITIZER")
endif()
target_compile_options(instantee_build_options INTERFACE ${sanitizer_flags})
target_link_options(instantee_build_options INTERFACE ${sanitizer_flags})
