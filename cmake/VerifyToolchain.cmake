include("${CMAKE_CURRENT_LIST_DIR}/ToolchainLock.cmake")

set(INSTANTEE_TOOLCHAIN_MODE "certification" CACHE STRING
  "certification locks exact reference versions; development keeps feature probes")
set_property(CACHE INSTANTEE_TOOLCHAIN_MODE PROPERTY STRINGS
  certification development)

function(instantee_require_equal label actual required)
  if(NOT "${actual}" STREQUAL "${required}")
    message(FATAL_ERROR "${label} differs from the toolchain lock: expected ${required}")
  endif()
endfunction()

# Shared platform requirements for every native configure.
instantee_require_equal("generator" "${CMAKE_GENERATOR}" "Ninja")
instantee_require_equal("platform" "${CMAKE_SYSTEM_NAME}" "Darwin")
instantee_require_equal("architecture" "${CMAKE_OSX_ARCHITECTURES}" "arm64")
if(CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "Toolchain probes require native execution on the reference platform")
endif()
foreach(language IN ITEMS CXX OBJCXX)
  instantee_require_equal("${language} compiler" "${CMAKE_${language}_COMPILER_ID}" "AppleClang")
endforeach()

if(INSTANTEE_TOOLCHAIN_MODE STREQUAL "certification")
  instantee_require_equal("CMake" "${CMAKE_VERSION}" "${INSTANTEE_CMAKE_VERSION}")
  instantee_require_equal("deployment target" "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    "${INSTANTEE_DEPLOYMENT_TARGET}")
  foreach(language IN ITEMS CXX OBJCXX)
    instantee_require_equal("${language} version"
      "${CMAKE_${language}_COMPILER_VERSION}" "${INSTANTEE_CLANG_VERSION}")
  endforeach()

  execute_process(COMMAND "${CMAKE_MAKE_PROGRAM}" --version
    OUTPUT_VARIABLE ninja_version OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE result TIMEOUT 10)
  instantee_require_equal("Ninja query" "${result}" "0")
  instantee_require_equal("Ninja" "${ninja_version}" "${INSTANTEE_NINJA_VERSION}")
  execute_process(COMMAND /usr/bin/xcodebuild -version
    OUTPUT_VARIABLE xcode_version OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    RESULT_VARIABLE result TIMEOUT 15)
  instantee_require_equal("Xcode query" "${result}" "0")
  instantee_require_equal("Xcode" "${xcode_version}" "${INSTANTEE_XCODE_VERSION}")
  file(READ "${CMAKE_OSX_SYSROOT}/SDKSettings.json" sdk_settings)
  string(JSON sdk_version GET "${sdk_settings}" Version)
  execute_process(COMMAND /usr/libexec/PlistBuddy -c Print:ProductBuildVersion
    "${CMAKE_OSX_SYSROOT}/System/Library/CoreServices/SystemVersion.plist"
    OUTPUT_VARIABLE sdk_build OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE result TIMEOUT 10)
  instantee_require_equal("SDK build query" "${result}" "0")
  instantee_require_equal("SDK version" "${sdk_version}" "${INSTANTEE_SDK_VERSION}")
  instantee_require_equal("SDK build" "${sdk_build}" "${INSTANTEE_SDK_BUILD}")

  find_program(INSTANTEE_NODE node REQUIRED)
  execute_process(COMMAND "${INSTANTEE_NODE}" tools/toolchain/check-node.cjs
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" RESULT_VARIABLE result TIMEOUT 10)
  instantee_require_equal("Node/TypeScript lock (run npm ci first)" "${result}" "0")
elseif(INSTANTEE_TOOLCHAIN_MODE STREQUAL "development")
  message(STATUS "Instantee development toolchain mode: compatibility + feature probes; Node is optional")
else()
  message(FATAL_ERROR "Unsupported INSTANTEE_TOOLCHAIN_MODE=${INSTANTEE_TOOLCHAIN_MODE}")
endif()
