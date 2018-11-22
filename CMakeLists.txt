cmake_minimum_required(VERSION 3.10)

project(alta)

# we only need to add Talta
# it'll automatically pull in AltaCore as well
add_subdirectory("${PROJECT_SOURCE_DIR}/deps/talta")

add_subdirectory("${PROJECT_SOURCE_DIR}/deps/crossguid")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED on)
set(CMAKE_CXX_EXTENSIONS off)

add_executable(altac
  "${PROJECT_SOURCE_DIR}/src/main.cpp"
  "${PROJECT_SOURCE_DIR}/src/printers.cpp"
)
target_include_directories(altac PRIVATE "${PROJECT_SOURCE_DIR}/include")

target_link_libraries(altac PUBLIC altacore talta crossguid)


# copy the stl directory to the build tree
add_custom_target(dummy_alta_stl_copy_target ALL DEPENDS dummy_alta_stl_output)
add_custom_command(
  OUTPUT dummy_alta_stl_output
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/stl" "$<TARGET_FILE_DIR:altac>/stl"
  DEPENDS dummy_alta_always_rebuild
)
add_custom_command(
  OUTPUT dummy_alta_always_rebuild
  COMMAND ${CMAKE_COMMAND} -E echo
)

# define the stl directory for debug builds
target_compile_definitions(altac PRIVATE ALTA_DEBUG_STL_PATH="$<TARGET_FILE_DIR:altac>/stl")
