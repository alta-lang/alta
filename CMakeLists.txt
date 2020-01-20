cmake_minimum_required(VERSION 3.10)

project(alta
  VERSION 0.6.3
  DESCRIPTION "A compiler for the Alta language"
  HOMEPAGE_URL "https://github.com/alta-lang/alta"
  LANGUAGES C CXX
)

# `_version.hpp` is never actually produced, it's just there to force
# CMake to always rerun this command
add_custom_command(
  OUTPUT "${PROJECT_BINARY_DIR}/gen/include/alta/version.hpp"
         "${PROJECT_BINARY_DIR}/gen/include/alta/_version.hpp"
  COMMAND ${CMAKE_COMMAND} "-DPROJECT_BINARY_DIR=${PROJECT_BINARY_DIR}" -P "${PROJECT_SOURCE_DIR}/CMake/version.cmake"
)
add_custom_target(alta-version-header
  DEPENDS "${PROJECT_BINARY_DIR}/gen/include/alta/version.hpp"
)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif()

# we only need to add Talta
# it'll automatically pull in AltaCore as well
# btw, we add EXCLUDE_FROM_ALL to prevent the the subdirectories
# from installing anything
add_subdirectory("${PROJECT_SOURCE_DIR}/deps/talta" EXCLUDE_FROM_ALL)

add_executable(altac
  "${PROJECT_SOURCE_DIR}/src/altac/main.cpp"
  "${PROJECT_SOURCE_DIR}/src/altac/printers.cpp"
  "${PROJECT_SOURCE_DIR}/src/altac/cli-parser.cpp"
)
add_dependencies(altac
  alta-version-header
)
target_include_directories(altac
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
  PRIVATE "${PROJECT_SOURCE_DIR}/deps/jsoncpp"
  PRIVATE "${PROJECT_BINARY_DIR}/gen/include"
)

# credit to Austin Lasher (https://medium.com/@alasher/colored-c-compiler-output-with-ninja-clang-gcc-10bfe7f2b949)
option(FORCE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." TRUE)
if(${FORCE_COLORED_OUTPUT})
  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    target_compile_options(altac PRIVATE -fdiagnostics-color=always)
  elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    target_compile_options(altac PRIVATE -fcolor-diagnostics)
  endif()
endif()

target_link_libraries(altac PUBLIC altacore talta crossguid)

# define the stdlib directory for debug builds
target_compile_definitions(altac PRIVATE ALTA_DEBUG_STDLIB_PATH="${PROJECT_SOURCE_DIR}/stdlib" PRIVATE ALTA_DEBUG_RUNTIME_PATH="${PROJECT_SOURCE_DIR}/runtime")

set_target_properties(altac
  PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED on
  CXX_EXTENSIONS off
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

FILE(GLOB_RECURSE AMAKE_SOURCES FOLLOW_SYMLINKS LIST_DIRECTORIES true ${PROJECT_BINARY_DIR}/*)

# we use `runtime-override` and `stdlib-override` to ensure
# the right paths are used in release builds
add_custom_target(amake ALL
  "$<TARGET_FILE:altac>" -c -g "${CMAKE_GENERATOR}" -o "${PROJECT_BINARY_DIR}/amake" -nw msvc-crt-secure --runtime-override "${PROJECT_SOURCE_DIR}/runtime" --stdlib-override "${PROJECT_SOURCE_DIR}/stdlib" "${PROJECT_SOURCE_DIR}/src/amake"
  SOURCES ${AMAKE_SOURCES}
  USES_TERMINAL
)

if (WIN32)
  set(PLATFORM_EXECUTABLE_EXTENSION ".exe")
else()
  set(PLATFORM_EXECUTABLE_EXTENSION "")
endif()

add_custom_command(TARGET amake POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_BINARY_DIR}/amake/_build/bin/amake${PLATFORM_EXECUTABLE_EXTENSION}" "${CMAKE_BINARY_DIR}/bin/amake${PLATFORM_EXECUTABLE_EXTENSION}"
)

if (WIN32)
  set(ALTA_DOC_DIR "doc")
else()
  set(ALTA_DOC_DIR "share/doc/alta")
endif()

# install AppStream metadata on Linux
install(FILES "${PROJECT_SOURCE_DIR}/CMake/CPack/Resources/org.alta.alta.metainfo.xml"
  DESTINATION "share/metainfo"
  COMPONENT Linux
)

# install the machine-readable license for Debian systems
install(FILES "${PROJECT_SOURCE_DIR}/CMake/CPack/Resources/copyright"
  DESTINATION "${ALTA_DOC_DIR}"
  COMPONENT Debian
)
# install a regular license notice for all systems
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
  DESTINATION "${ALTA_DOC_DIR}"
  COMPONENT Core
  RENAME "LICENSE.txt"
)
install(FILES "${PROJECT_SOURCE_DIR}/CMake/CPack/Resources/Doc.README.md"
  DESTINATION "${ALTA_DOC_DIR}"
  COMPONENT Core
  RENAME "README.md"
)

# install additional copyright notices
install(FILES "${PROJECT_SOURCE_DIR}/CMake/CPack/Resources/Licenses.README.md"
  DESTINATION "${ALTA_DOC_DIR}/additional-licenses"
  COMPONENT Core
  RENAME "README.md"
)
install(FILES "${PROJECT_SOURCE_DIR}/deps/json.LICENSE.txt"
  DESTINATION "${ALTA_DOC_DIR}/additional-licenses"
  COMPONENT Core
)
install(FILES "${PROJECT_SOURCE_DIR}/deps/talta/deps/PicoSHA2/LICENSE"
  DESTINATION "${ALTA_DOC_DIR}/additional-licenses"
  COMPONENT Core
  RENAME "PicoSHA2.LICENSE.txt"
)
install(FILES "${PROJECT_SOURCE_DIR}/deps/talta/deps/alta-core/deps/any/LICENSE_1_0.txt"
  DESTINATION "${ALTA_DOC_DIR}/additional-licenses"
  COMPONENT Core
  RENAME "any.LICENSE.txt"
)
install(FILES "${PROJECT_SOURCE_DIR}/deps/talta/deps/alta-core/deps/crossguid/LICENSE"
  DESTINATION "${ALTA_DOC_DIR}/additional-licenses"
  COMPONENT Core
  RENAME "crossguid.LICENSE.txt"
)
install(FILES "${PROJECT_SOURCE_DIR}/deps/talta/deps/alta-core/deps/optional/COPYING"
  DESTINATION "${ALTA_DOC_DIR}/additional-licenses"
  COMPONENT Core
  RENAME "optional.LICENSE.txt"
)
install(FILES "${PROJECT_SOURCE_DIR}/deps/talta/deps/alta-core/deps/semver.c/LICENSE"
  DESTINATION "${ALTA_DOC_DIR}/additional-licenses"
  COMPONENT Core
  RENAME "semver.c.LICENSE.txt"
)
install(FILES "${PROJECT_SOURCE_DIR}/deps/talta/deps/alta-core/deps/variant/LICENSE.md"
  DESTINATION "${ALTA_DOC_DIR}/additional-licenses"
  COMPONENT Core
  RENAME "variant.LICENSE.txt"
)
install(FILES "${PROJECT_SOURCE_DIR}/deps/talta/deps/alta-core/deps/yaml-cpp/LICENSE"
  DESTINATION "${ALTA_DOC_DIR}/additional-licenses"
  COMPONENT Core
  RENAME "yaml-cpp.LICENSE.txt"
)
install(FILES "${PROJECT_SOURCE_DIR}/CMake/CPack/Resources/Boost--Filesystem.LICENSE.txt"
  DESTINATION "${ALTA_DOC_DIR}/additional-licenses"
  COMPONENT Core
)

# install documentation
install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs/language-reference/"
  DESTINATION "${ALTA_DOC_DIR}/language-reference"
  COMPONENT Documentation
  FILES_MATCHING PATTERN "*"
)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs/stdlib"
  DESTINATION "${ALTA_DOC_DIR}/stdlib"
  COMPONENT Documentation
  FILES_MATCHING PATTERN "*"
)

# install files for altac
if (WIN32)
  install(TARGETS altac
    RUNTIME DESTINATION .
    COMPONENT Core
  )
else()
  install(TARGETS altac
    RUNTIME DESTINATION bin
    COMPONENT Core
  )
endif()

set(AMAKE_EXECUTABLE "${CMAKE_BINARY_DIR}/bin/amake${PLATFORM_EXECUTABLE_EXTENSION}")

# install files for amake
if (WIN32)
  install(PROGRAMS "${AMAKE_EXECUTABLE}"
    DESTINATION .
    COMPONENT Utilities
  )
else()
  install(PROGRAMS "${AMAKE_EXECUTABLE}"
    DESTINATION bin
    COMPONENT Utilities
  )
endif()

# install standard library
if (WIN32)
  install(DIRECTORY "${PROJECT_SOURCE_DIR}/stdlib/"
    DESTINATION stdlib
    COMPONENT StandardLibrary
    FILES_MATCHING PATTERN "*"
  )
else()
  install(DIRECTORY "${PROJECT_SOURCE_DIR}/stdlib/"
    DESTINATION lib/alta-stdlib
    COMPONENT StandardLibrary
    FILES_MATCHING PATTERN "*"
  )
endif()

# install runtime
if (WIN32)
  install(DIRECTORY "${PROJECT_SOURCE_DIR}/runtime/"
    DESTINATION runtime
    COMPONENT Core
    FILES_MATCHING PATTERN "*"
  )
else()
  install(DIRECTORY "${PROJECT_SOURCE_DIR}/runtime/"
    DESTINATION lib/alta-runtime
    COMPONENT Core
    FILES_MATCHING PATTERN "*"
  )
endif()

include(InstallRequiredSystemLibraries)
include(CPackComponent)

configure_file(
  "${PROJECT_SOURCE_DIR}/CMake/cpack-config.cmake.in"
  "${PROJECT_BINARY_DIR}/cpack-config.cmake"
  IMMEDIATE @ONLY
)

set(CPACK_PROJECT_CONFIG_FILE "${PROJECT_BINARY_DIR}/cpack-config.cmake")

set(CPACK_PACKAGE_NAME "Alta")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Essentials for Alta development")
set(CPACK_PACKAGE_VENDOR "Alta Project Contributors")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${PROJECT_SOURCE_DIR}/CMake/CPack/Resources/README.txt")
set(CPACK_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/CMake/CPack/Resources/README.txt")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/CMake/CPack/Resources/LICENSE.txt")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_NAME}")
set(CPACK_STRIP_FILES TRUE)
set(CPACK_PACKAGE_CONTACT "Ariel Abreu <facekapow@outlook.com>")

cpack_add_install_type(full DISPLAY_NAME "Full")
cpack_add_install_type(minimal DISPLAY_NAME "Minimal")

cpack_add_component(Core
  DISPLAY_NAME "Core"
  DESCRIPTION "Core compiler resources"
  REQUIRED
  INSTALL_TYPES full minimal
)
cpack_add_component(Utilities
  DISPLAY_NAME "Utilities"
  DESCRIPTION "Useful tools for development with Alta"
  INSTALL_TYPES full
)
cpack_add_component(StandardLibrary
  DISPLAY_NAME "Standard Library"
  DESCRIPTION "Alta's standard library. Highly recommended"
  INSTALL_TYPES full
)
cpack_add_component(Documentation
  DISPLAY_NAME "Documention"
  DESCRIPTION "Offline copy of Alta's documentation"
  INSTALL_TYPES full
)
cpack_add_component(Debian
  DISPLAY_NAME "Debian"
  DESCRIPTION "Hidden component for additional Debian-packaging files"
)
cpack_add_component(Linux
  DISPLAY_NAME "Linux"
  DESCRIPTION "Hidden component for additional Linux-packaging files"
)

# remove the `dev` component (from yaml-cpp) and the `Unspecified` component
# also remove the `Debian` component by default for all packages
get_cmake_property(CPACK_COMPONENTS_ALL COMPONENTS)
list(REMOVE_ITEM CPACK_COMPONENTS_ALL "dev" "Unspecified" "Debian" "Linux")

set(CPACK_NSIS_COMPONENT_INSTALL TRUE)
set(CPACK_NSIS64_COMPONENT_INSTALL TRUE)
set(CPACK_WIX_COMPONENT_INSTALL TRUE)
set(CPACK_productbuild_COMPONENT_INSTALL TRUE)
set(CPACK_7Z_COMPONENT_INSTALL FALSE)
set(CPACK_ZIP_COMPONENT_INSTALL FALSE)
set(CPACK_TGZ_COMPONENT_INSTALL FALSE)
set(CPACK_TXZ_COMPONENT_INSTALL FALSE)
set(CPACK_DEB_COMPONENT_INSTALL FALSE)
set(CPACK_RPM_COMPONENT_INSTALL FALSE)

if (WIN32)
  # set variables for NSIS in case anyone wants to use it
  #
  # Alta will not build with NSIS by default, however, due to
  # issues with it not properly updating the PATH variable
  set(CPACK_NSIS_MODIFY_PATH TRUE)
  set(CPACK_NSIS_DISPLAY_NAME "${CPACK_PACKAGE_NAME}")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL TRUE)
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
  set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "${CPACK_PACKAGE_NAME}")
  set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")

  # instead, we use WiX by default
  #
  # in order to be able to add things to the PATH with WiX,
  # we need to create a patch and tell CPack to use it
  set(CPACK_WIX_EXTRA_SOURCES "${PROJECT_SOURCE_DIR}/CMake/CPack/Resources/mondo.wxs" "${PROJECT_SOURCE_DIR}/CMake/CPack/Resources/extra-dialog.wxs")
  set(CPACK_WIX_PATCH_FILE "${PROJECT_SOURCE_DIR}/CMake/CPack/Resources/WiX.patch.xml")
  set(CPACK_WIX_UI_REF "AltaUI_Mondo")
  # A17A = ALTA
  set(CPACK_WIX_UPGRADE_GUID "A17AA17A-A17A-A17A-A17A-A17AA17AA17A")

  set(CPACK_GENERATOR "7Z;ZIP;WIX")
elseif (APPLE)
  set(CPACK_GENERATOR "ZIP;productbuild")
elseif (UNIX)
  # rpm
  set(CPACK_RPM_PACKAGE_LICENSE "MIT")
  set(CPACK_PACKAGE_RELOCATABLE TRUE)
  set(CPACK_RPM_CHANGELOG_FILE "${PROJECT_SOURCE_DIR}/CHANGELOG.md")
  set(CPACK_RPM_PACKAGE_REQUIRES "libuuid >= 2.34")
  set(CPACK_RPM_PACKAGE_AUTOREQ TRUE)
  set(CPACK_RPM_CHANGELOG_FILE "${PROJECT_SOURCE_DIR}/CMake/CPack/Resources/changelog.rpm.txt")

  # debian
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libuuid1 (>= 2.25)")
  set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "build-essential (>= 12.4)")

  # setting generators
  set(CPACK_GENERATOR "TGZ;TXZ")

  find_program(DPKG_COMMAND dpkg)
  find_program(RPMBUILD_COMMAND rpmbuild)

  if (DPKG_COMMAND)
    set(CPACK_GENERATOR "${CPACK_GENERATOR};DEB")
  endif()

  if (RPMBUILD_COMMAND)
    set(CPACK_GENERATOR "${CPACK_GENERATOR};RPM")
  endif()
else()
  message(WARNING "Creating a tailored installer is not supported for the current platform, only archive distributions will be available.")

  set(CPACK_GENERATOR "ZIP;TGZ;TXZ")
endif()

include(CPack)

# uninstall target
if (NOT TARGET uninstall)
  configure_file(
    "${PROJECT_SOURCE_DIR}/CMake/uninstall.cmake.in"
    "${PROJECT_BINARY_DIR}/uninstall.cmake"
    IMMEDIATE @ONLY
  )
  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${PROJECT_BINARY_DIR}/uninstall.cmake
  )
endif()
