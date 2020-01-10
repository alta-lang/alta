namespace Alta {
  namespace Version {
    const char* const commit = \"${ALTA_GIT_COMMIT}${ALTA_GIT_DIRTY}\";
    const char* const plainCommit = \"${ALTA_GIT_COMMIT}\";
    const bool dirty = ${ALTA_GIT_DIRTY_LITERAL};
    const char* const revision = \"${ALTA_GIT_REVISION}\";
    const char* const version = \"${ALTA_GIT_VERSION}\";
    const int major = ${ALTA_GIT_VERSION_MAJOR};
    const int minor = ${ALTA_GIT_VERSION_MINOR};
    const int patch = ${ALTA_GIT_VERSION_PATCH};
  };
};
