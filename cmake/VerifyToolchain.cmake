include("${CMAKE_CURRENT_LIST_DIR}/ToolchainLock.cmake")

function(instantee_require_equal label actual required)
  if(NOT "${actual}" STREQUAL "${required}")
    message(FATAL_ERROR "${label} differs from the toolchain lock: expected ${required}")
  endif()
endfunction()

instantee_require_equal("CMake" "${CMAKE_VERSION}" "${INSTANTEE_CMAKE_VERSION}")
instantee_require_equal("generator" "${CMAKE_GENERATOR}" "Ninja")
instantee_require_equal("platform" "${CMAKE_SYSTEM_NAME}" "Darwin")
instantee_require_equal("architecture" "${CMAKE_OSX_ARCHITECTURES}" "arm64")
instantee_require_equal("deployment target" "${CMAKE_OSX_DEPLOYMENT_TARGET}" "${INSTANTEE_DEPLOYMENT_TARGET}")
if(CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "Toolchain probes require native execution on the reference platform")
endif()
foreach(language IN ITEMS CXX OBJCXX)
  instantee_require_equal("${language} compiler" "${CMAKE_${language}_COMPILER_ID}" "AppleClang")
  instantee_require_equal("${language} version" "${CMAKE_${language}_COMPILER_VERSION}" "${INSTANTEE_CLANG_VERSION}")
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
# Read the SDK actually selected by CMake, including an explicit sysroot override.
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
