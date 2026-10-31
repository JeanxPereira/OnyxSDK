if(NOT TARGET OnyxBox)
    add_subdirectory(OnyxBox)
endif()
if(ONYX_WITH_SHELL)
    add_subdirectory(MinimalViewer)
endif()
# Embedded fetches GLFW for its own window, Shell or not; nothing outside the examples sees it, and
# with ONYX_BUILD_EXAMPLES off neither the example nor the fetch exists.
if(ONYX_WITH_UI)
    add_subdirectory(Embedded)
endif()
