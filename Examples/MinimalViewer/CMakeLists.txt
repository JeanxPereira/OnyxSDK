# -- MinimalViewer -- second Onyx consumer (boundary proof) ------------------
# Knows nothing about God of War. Links Onyx::Onyx plus the OnyxBox example
# module (Onyx_ExampleBox) it registers with the Workspace at boot (M3b).

file(GLOB MINIMALVIEWER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
)

add_executable(MinimalViewer ${MINIMALVIEWER_SOURCES})
target_include_directories(MinimalViewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
# Onyx::Render rides in transitively via Onyx::Shell (T9: Window.cpp owns
# the VkContext/swapchain everything presents through) -- named explicitly
# here too since Main.cpp's boot path is exactly what T9's proof run drives.
# Onyx_ExampleBox is example-local (Examples/OnyxBox), not an SDK target,
# so it has no Onyx:: alias to name -- every SDK target here does.
target_link_libraries(MinimalViewer PRIVATE Onyx::Onyx Onyx::Render Onyx_ExampleBox)

if(WIN32)
    set_target_properties(MinimalViewer PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
endif()

if(NOT APPLE)
    add_custom_command(TARGET MinimalViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ONYX_FONTS_DIR}" "$<TARGET_FILE_DIR:MinimalViewer>/third_party/fonts"
        COMMENT "Copying fonts next to MinimalViewer")
endif()

if(WIN32 AND ONYX_FFMPEG_DLLS)
    add_custom_command(TARGET MinimalViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ONYX_FFMPEG_DLLS} "$<TARGET_FILE_DIR:MinimalViewer>"
        COMMENT "Copying FFmpeg DLLs next to MinimalViewer")
endif()
