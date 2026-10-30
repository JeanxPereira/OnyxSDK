function(onyx_version)
    set(ONYX_VERSION_MAJOR 2)
    set(ONYX_VERSION_MINOR 0)
    set(ONYX_VERSION_PATCH 0)
    set(ONYX_VERSION_STRING "${ONYX_VERSION_MAJOR}.${ONYX_VERSION_MINOR}.${ONYX_VERSION_PATCH}")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/Version.h.in"
                   "${PROJECT_BINARY_DIR}/generated/Onyx/Core/Version.h" @ONLY)
endfunction()
onyx_version()
