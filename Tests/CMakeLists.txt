# ── Onyx engine-only unit tests (doctest) ─────────────────────────────────
# doctest is fetched by the root CMakeLists.txt when ONYX_BUILD_TESTS=ON.

add_executable(onyx_tests
    main.cpp
    sanity_test.cpp
    logger_test.cpp
    metrics_test.cpp
    threading_test.cpp
    theme_contrast_test.cpp
    vfs_transformfile_test.cpp
)
target_link_libraries(onyx_tests PRIVATE Onyx::Onyx doctest::doctest imgui_lib)
if(MSVC)
    target_compile_options(onyx_tests PRIVATE /utf-8)
endif()

add_test(NAME OnyxUnit          COMMAND onyx_tests)
add_test(NAME OnyxLogger        COMMAND onyx_tests "--test-case=*Logger*")
add_test(NAME OnyxMetrics       COMMAND onyx_tests "--test-case=*Metrics*")
add_test(NAME OnyxThreading     COMMAND onyx_tests "--test-case=*Threading*")
add_test(NAME OnyxThemeContrast COMMAND onyx_tests "--test-case=*ThemeContrast*")
add_test(NAME OnyxTransformFile COMMAND onyx_tests "--test-case=*TransformFile*,*MakeXorFile*")

add_test(NAME MinimalViewer_SelfTest
    COMMAND MinimalViewer --selftest $<TARGET_FILE:MinimalViewer>)
set_tests_properties(MinimalViewer_SelfTest PROPERTIES
    PASS_REGULAR_EXPRESSION "selftest OK")
