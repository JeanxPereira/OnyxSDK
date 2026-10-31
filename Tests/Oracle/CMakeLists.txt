# The oracle renders the frozen corpus and gates the renderer on it; its text helpers are unit tested
# without a GPU.
add_executable(OnyxOracleTests ReportTests.cpp Report.cpp)
target_link_libraries(OnyxOracleTests PRIVATE Onyx::Render Onyx::TestKit doctest::doctest)
onyx_warnings(OnyxOracleTests)
set_target_properties(OnyxOracleTests PROPERTIES CXX_SCAN_FOR_MODULES OFF)
add_test(NAME Oracle.Unit COMMAND OnyxOracleTests)

add_executable(OnyxOracle Main.cpp Commands.cpp Report.cpp)
target_link_libraries(OnyxOracle PRIVATE Onyx::Render Onyx::TestKit)
onyx_warnings(OnyxOracle)
set_target_properties(OnyxOracle PROPERTIES CXX_SCAN_FOR_MODULES OFF)

set(golden "${PROJECT_SOURCE_DIR}/Tests/Golden")
add_test(NAME Render.Parity
    COMMAND OnyxOracle parity --golden ${golden}/corpus --out ${CMAKE_CURRENT_BINARY_DIR}/parity
            --tolerances ${golden}/tolerances.toml)
add_test(NAME Render.Reproducible
    COMMAND OnyxOracle reproducible --out ${CMAKE_CURRENT_BINARY_DIR}/repro)
set_tests_properties(Render.Parity Render.Reproducible PROPERTIES SKIP_RETURN_CODE 77 LABELS render)
