# onyx_install_assets(<target>)
# Copies the bundled fonts next to the executable, into <target dir>/assets/fonts.
function(onyx_install_assets target)
    set(font_dir "${PROJECT_SOURCE_DIR}/third_party/fonts")
    set(dest "$<TARGET_FILE_DIR:${target}>/assets/fonts")
    foreach(font SFMono-Regular.otf SFSymbols.ttf)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${dest}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${font_dir}/${font}" "${dest}/${font}")
    endforeach()
endfunction()

# onyx_install_media_runtime(<target>)
# Copies the FFmpeg shared libraries next to the executable on Windows; elsewhere the system
# libraries are found by the loader.
function(onyx_install_media_runtime target)
    if(NOT WIN32)
        return()
    endif()
    foreach(dll ${ONYX_FFMPEG_DLLS})
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:${target}>")
    endforeach()
endfunction()
