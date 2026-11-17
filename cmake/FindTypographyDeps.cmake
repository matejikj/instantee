# Resolves HarfBuzz, FreeType and ICU via pkg-config. Soft-fails into empty
# imported targets only when OPTIONAL is requested; Instantee always requires them.

find_package(PkgConfig REQUIRED)

# Homebrew icu4c is keg-only; prepend its pkgconfig if present.
execute_process(
  COMMAND brew --prefix icu4c@79
  OUTPUT_VARIABLE _instantee_icu_prefix
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE _instantee_icu_brew_rc)
if(_instantee_icu_brew_rc EQUAL 0 AND EXISTS "${_instantee_icu_prefix}/lib/pkgconfig")
  set(ENV{PKG_CONFIG_PATH}
    "${_instantee_icu_prefix}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()

pkg_check_modules(INSTANTEE_HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz>=15.0.0)
pkg_check_modules(INSTANTEE_FREETYPE REQUIRED IMPORTED_TARGET freetype2>=24.0.0)
pkg_check_modules(INSTANTEE_ICU_UC REQUIRED IMPORTED_TARGET icu-uc>=74.0)

message(STATUS "typography deps: harfbuzz ${INSTANTEE_HARFBUZZ_VERSION}, "
               "freetype2 ${INSTANTEE_FREETYPE_VERSION}, "
               "icu-uc ${INSTANTEE_ICU_UC_VERSION}")
