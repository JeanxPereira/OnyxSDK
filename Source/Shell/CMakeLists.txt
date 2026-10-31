include(${PROJECT_SOURCE_DIR}/cmake/Deps/Glfw.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/Deps/Imgui.cmake)
onyx_dep_glfw()
onyx_dep_imgui()

onyx_add_layer(Shell PUBLIC_DEPS Onyx::Ui imgui::glfw glfw)
