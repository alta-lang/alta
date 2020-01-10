execute_process(
  COMMAND git describe --long --tags
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE ALTA_GIT_DESC
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

if ("${ALTA_GIT_DESC}" STREQUAL "")
  set(ALTA_GIT_VERSION_MAJOR "0")
  set(ALTA_GIT_VERSION_MINOR "0")
  set(ALTA_GIT_VERSION_PATCH "0")
  set(ALTA_GIT_VERSION "0.0.0")
  set(ALTA_GIT_REVISION "0")
  set(ALTA_GIT_COMMIT "0000000")
  set(ALTA_GIT_DIRTY "")
  set(ALTA_GIT_DIRTY_LITERAL "false")
else()
  string(REPLACE "-" ";" ALTA_GIT_DESC_LIST "${ALTA_GIT_DESC}")

  list(POP_FRONT ALTA_GIT_DESC_LIST ALTA_GIT_VERSION)
  list(POP_FRONT ALTA_GIT_DESC_LIST ALTA_GIT_REVISION)
  list(POP_FRONT ALTA_GIT_DESC_LIST ALTA_GIT_COMMIT)

  string(REGEX REPLACE "^v" "" ALTA_GIT_VERSION "${ALTA_GIT_VERSION}")
  string(REGEX REPLACE "^g" "" ALTA_GIT_COMMIT "${ALTA_GIT_COMMIT}")

  string(REPLACE "." ";" ALTA_GIT_VERSION_LIST "${ALTA_GIT_VERSION}")

  list(POP_FRONT ALTA_GIT_VERSION_LIST ALTA_GIT_VERSION_MAJOR)
  list(POP_FRONT ALTA_GIT_VERSION_LIST ALTA_GIT_VERSION_MINOR)
  list(POP_FRONT ALTA_GIT_VERSION_LIST ALTA_GIT_VERSION_PATCH)

  execute_process(
    COMMAND git diff --quiet --exit-code
    RESULT_VARIABLE ALTA_MODIFIED
  )
  if ("${ALTA_MODIFIED}" EQUAL "1")
    set(ALTA_GIT_DIRTY "-dirty")
    set(ALTA_GIT_DIRTY_LITERAL "true")
  else()
    set(ALTA_GIT_DIRTY "")
    set(ALTA_GIT_DIRTY_LITERAL "false")
  endif()

  if (("${ALTA_GIT_REVISION}" EQUAL "0") AND (NOT ("${ALTA_GIT_DIRTY}" STREQUAL "-dirty")))
    set(ALTA_GIT_MAYBE_COMMIT "")
  else()
    set(ALTA_GIT_MAYBE_COMMIT "-${ALTA_GIT_COMMIT}")
  endif()
endif()

set(ALTA_VERSION_FILE_CONTENTS "#ifndef _ALTA_VERSION_HPP
#define _ALTA_VERSION_HPP
namespace Alta {
  namespace Version {
    const char* const commit = \"${ALTA_GIT_COMMIT}${ALTA_GIT_DIRTY}\";
    const char* const plainCommit = \"${ALTA_GIT_COMMIT}\";
    const bool dirty = ${ALTA_GIT_DIRTY_LITERAL};
    const char* const revision = \"${ALTA_GIT_REVISION}\";
    const char* const version = \"${ALTA_GIT_VERSION}${ALTA_GIT_MAYBE_COMMIT}${ALTA_GIT_DIRTY}\";
    const char* const plainVersion = \"${ALTA_GIT_VERSION}\";
    const int major = ${ALTA_GIT_VERSION_MAJOR};
    const int minor = ${ALTA_GIT_VERSION_MINOR};
    const int patch = ${ALTA_GIT_VERSION_PATCH};
  };
};
#endif /* _ALTA_VERSION_HPP */")

if (EXISTS "${PROJECT_BINARY_DIR}/gen/include/alta/version.hpp")
  file(READ "${PROJECT_BINARY_DIR}/gen/include/alta/version.hpp" ALTA_VERSION_FILE_CURRENT_CONTENTS)
else()
  set(ALTA_VERSION_FILE_CURRENT_CONTENTS "")
endif()

if (NOT "${ALTA_VERSION_FILE_CONTENTS}" STREQUAL "${ALTA_VERSION_FILE_CURRENT_CONTENTS}")
  file(WRITE "${PROJECT_BINARY_DIR}/gen/include/alta/version.hpp" "${ALTA_VERSION_FILE_CONTENTS}")
endif()
