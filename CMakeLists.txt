cmake_minimum_required(VERSION 3.10)

project(alta)

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
target_include_directories(altac
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
  PRIVATE "${PROJECT_SOURCE_DIR}/deps/jsoncpp"
)

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

# install files for altac
if (WIN32)
  install(TARGETS altac
    RUNTIME DESTINATION .
  )
else()
  install(TARGETS altac
    RUNTIME DESTINATION bin
  )
endif()

set(AMAKE_EXECUTABLE "${CMAKE_BINARY_DIR}/bin/amake${PLATFORM_EXECUTABLE_EXTENSION}")

# install files for amake
if (WIN32)
  install(PROGRAMS "${AMAKE_EXECUTABLE}"
    DESTINATION .
  )
else()
  install(PROGRAMS "${AMAKE_EXECUTABLE}"
    DESTINATION bin
  )
endif()

# install standard library
if (WIN32)
  install(DIRECTORY "${PROJECT_SOURCE_DIR}/stdlib/"
    DESTINATION stdlib
    FILES_MATCHING PATTERN "*"
  )
else()
  install(DIRECTORY "${PROJECT_SOURCE_DIR}/stdlib/"
    DESTINATION lib/alta-stdlib
    FILES_MATCHING PATTERN "*"
  )
endif()

# install runtime
if (WIN32)
  install(DIRECTORY "${PROJECT_SOURCE_DIR}/runtime/"
    DESTINATION runtime
    FILES_MATCHING PATTERN "*"
  )
else()
  install(DIRECTORY "${PROJECT_SOURCE_DIR}/runtime/"
    DESTINATION lib/alta-runtime
    FILES_MATCHING PATTERN "*"
  )
endif()
