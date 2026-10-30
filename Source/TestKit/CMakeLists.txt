include(${PROJECT_SOURCE_DIR}/cmake/Deps/Stb.cmake)
onyx_dep_stb()
onyx_add_layer(TestKit PUBLIC_DEPS Onyx::Core DEPS stb)
