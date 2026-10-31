include(${PROJECT_SOURCE_DIR}/cmake/Deps/Imgui.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/Assets.cmake)
onyx_dep_imgui()

set(icon_header "${CMAKE_CURRENT_SOURCE_DIR}/include/Onyx/Ui/SfSymbols.h")
set(icon_table "${PROJECT_BINARY_DIR}/generated/Onyx/Ui/IconTable.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${icon_header}" "${PROJECT_SOURCE_DIR}/cmake/GenerateIconTable.cmake")
execute_process(
    COMMAND ${CMAKE_COMMAND} -DHEADER=${icon_header} -DOUTPUT=${icon_table}
            -P ${PROJECT_SOURCE_DIR}/cmake/GenerateIconTable.cmake
    RESULT_VARIABLE icon_result)
if(NOT icon_result EQUAL 0)
    message(FATAL_ERROR "Icon table generation failed")
endif()

onyx_add_layer(Ui
    SOURCES ${icon_table}
    PUBLIC_DEPS Onyx::Render imgui::imgui)
set_source_files_properties(${icon_table} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
