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
