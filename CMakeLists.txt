cmake_minimum_required(VERSION 3.10)

project(alta)

# we only need to add Talta
# it'll automatically pull in AltaCore as well
# btw, we add EXCLUDE_FROM_ALL to prevent the the subdirectories
# from installing anything
add_subdirectory("${PROJECT_SOURCE_DIR}/deps/talta" EXCLUDE_FROM_ALL)

add_executable(altac
  "${PROJECT_SOURCE_DIR}/src/main.cpp"
  "${PROJECT_SOURCE_DIR}/src/printers.cpp"
)
target_include_directories(altac
  PRIVATE "${PROJECT_SOURCE_DIR}/include"
  PRIVATE "${PROJECT_SOURCE_DIR}/deps/tclap/include"
)

target_link_libraries(altac PUBLIC altacore talta crossguid)

# define the stdlib directory for debug builds
target_compile_definitions(altac PRIVATE ALTA_DEBUG_STDLIB_PATH="${PROJECT_SOURCE_DIR}/stdlib")

set_target_properties(altac
  PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED on
  CXX_EXTENSIONS off
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# install files
if (WIN32)
  install(TARGETS altac
    RUNTIME DESTINATION .
  )
else()
  install(TARGETS altac
    RUNTIME DESTINATION bin
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
