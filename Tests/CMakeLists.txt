include(${PROJECT_SOURCE_DIR}/cmake/Deps/Doctest.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/StandaloneHeaders.cmake)
onyx_dep_doctest()

# onyx_add_tests(<Layer> [SOURCES <files>...] [LINK <targets>...])
# Builds Onyx<Layer>Tests from Tests/<Layer>/*.cpp (or SOURCES) and registers
# the ctest entry <Layer>.Unit.
function(onyx_add_tests layer)
    cmake_parse_arguments(ARG "" "" "SOURCES;LINK" ${ARGN})
    if(NOT ARG_SOURCES)
        file(GLOB ARG_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/Tests/${layer}/*.cpp")
    endif()
    add_executable(Onyx${layer}Tests ${ARG_SOURCES})
    target_link_libraries(Onyx${layer}Tests PRIVATE Onyx::${layer} doctest::doctest ${ARG_LINK})
    target_compile_definitions(Onyx${layer}Tests PRIVATE
        ONYX_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/Tests/Golden")
    onyx_warnings(Onyx${layer}Tests)
    add_test(NAME ${layer}.Unit COMMAND Onyx${layer}Tests)
    onyx_add_standalone_headers(${layer})
endfunction()

add_subdirectory(Core)
