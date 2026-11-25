# Relocatable install rules for the macOS CLI distribution package.
include(GNUInstallDirs)

set(_instantee_install_rpath "@loader_path/../lib")

if(TARGET instantee)
  set_target_properties(instantee PROPERTIES
    MACOSX_RPATH ON
    BUILD_WITH_INSTALL_RPATH OFF
    INSTALL_RPATH "${_instantee_install_rpath}"
    INSTALL_RPATH_USE_LINK_PATH FALSE
  )
  install(TARGETS instantee
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()

# Reserved directory for future private dylibs referenced by INSTALL_RPATH.
install(CODE
  "file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}\")"
)

set(_share "share/instantee")

install(DIRECTORY "${PROJECT_SOURCE_DIR}/contracts/schemas"
  DESTINATION "${_share}/contracts"
  FILES_MATCHING PATTERN "*.json"
)
install(DIRECTORY
  "${PROJECT_SOURCE_DIR}/contracts/fixtures/style-bible/positive"
  DESTINATION "${_share}/contracts/fixtures/style-bible"
  FILES_MATCHING PATTERN "*.json"
)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/third_party/fonts/"
  DESTINATION "${_share}/fonts"
  FILES_MATCHING
    PATTERN "*.ttf"
    PATTERN "LICENSE*"
    PATTERN "OFL*"
    PATTERN "PIN.json"
    PATTERN "README.md"
)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/authoring/packages/authoring/"
  DESTINATION "${_share}/authoring/packages/authoring"
  FILES_MATCHING
    PATTERN "package.json"
    PATTERN "PUBLIC_API.md"
    PATTERN "tsconfig.json"
    PATTERN "src/*"
    PATTERN "dist/*"
)
install(FILES
  "${PROJECT_SOURCE_DIR}/authoring/API.md"
  DESTINATION "${_share}/authoring"
)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/authoring/examples/tutorials/"
  DESTINATION "${_share}/examples/tutorials"
  FILES_MATCHING PATTERN "*.mjs"
)
install(FILES
  "${PROJECT_SOURCE_DIR}/packaging/macos/policy-v1.json"
  DESTINATION "${_share}"
)
