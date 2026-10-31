include(${PROJECT_SOURCE_DIR}/cmake/Deps/Cgltf.cmake)
onyx_dep_cgltf()
onyx_add_tests(Exchange LINK Onyx::TestKit Onyx::Cli cgltf::cgltf)
