file(GLOB PRISM_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_executable(OnyxPrismTests ${PRISM_TEST_SOURCES})
target_link_libraries(OnyxPrismTests PRIVATE Onyx::ExamplePrism Onyx::TestKit doctest::doctest)
target_compile_definitions(OnyxPrismTests PRIVATE
    ONYX_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/Tests/Golden"
    ONYX_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
onyx_warnings(OnyxPrismTests)
set_target_properties(OnyxPrismTests PROPERTIES CXX_SCAN_FOR_MODULES OFF)
add_test(NAME Prism.Unit COMMAND OnyxPrismTests)
