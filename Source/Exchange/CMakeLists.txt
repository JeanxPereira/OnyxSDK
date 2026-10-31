include(${PROJECT_SOURCE_DIR}/cmake/Deps/Cgltf.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/Deps/Stb.cmake)
onyx_dep_cgltf()
onyx_dep_stb()

onyx_add_layer(Exchange
    PUBLIC_DEPS Onyx::Core
    DEPS cgltf::cgltf Onyx_stb)
