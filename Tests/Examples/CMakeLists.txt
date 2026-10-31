# Examples.Build: MinimalViewer routes a command line to the CLI, which lists the verbs.
if(ONYX_WITH_SHELL)
    add_test(NAME Examples.Build
        COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:MinimalViewer>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckHelp.cmake)
endif()

# Examples.Embedded: the host with its own Vulkan device draws three frames, or skips without one.
if(ONYX_WITH_UI)
    add_test(NAME Examples.Embedded COMMAND Embedded --frames 3)
    set_tests_properties(Examples.Embedded PROPERTIES SKIP_RETURN_CODE 77)
endif()
