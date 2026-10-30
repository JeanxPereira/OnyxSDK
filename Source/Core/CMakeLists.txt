include(${PROJECT_SOURCE_DIR}/cmake/Deps/Glm.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/Deps/TomlPlusPlus.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/Deps/Lz4.cmake)
onyx_dep_glm()
onyx_dep_tomlplusplus()
onyx_dep_lz4()

onyx_add_layer(Core
    PUBLIC_DEPS glm::glm
    DEPS tomlplusplus::tomlplusplus Onyx_lz4)
target_include_directories(Onyx_Core PUBLIC "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>")
