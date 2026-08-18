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
    container_chunkreader_test.cpp
    container_chunkschema_test.cpp
    container_chunktree_test.cpp
    appconfig_test.cpp
    types_registry_test.cpp
    recentfiles_test.cpp
    assetvisibility_test.cpp
    profilemanager_test.cpp
    pathutils_test.cpp
    appearance_test.cpp
    framescheduler_test.cpp
    sessionlog_test.cpp
    typeregistrar_test.cpp
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
add_test(NAME OnyxChunkReader COMMAND onyx_tests "--test-case=*ChunkReader*")
add_test(NAME OnyxChunkSchema COMMAND onyx_tests "--test-case=*ChunkSchema*")
add_test(NAME OnyxChunkTree COMMAND onyx_tests "--test-case=*BuildChunkTree*")
add_test(NAME OnyxAppConfig COMMAND onyx_tests "--test-case=*AppConfig*")
add_test(NAME OnyxTypeRegistry    COMMAND onyx_tests "--test-case=*TypeCatalog*,*TypeRegistry*,*TypeRegistrar*")
add_test(NAME OnyxRecentFiles     COMMAND onyx_tests "--test-case=*RecentFiles*")
add_test(NAME OnyxAssetVisibility COMMAND onyx_tests "--test-case=*AssetVisibility*")
add_test(NAME OnyxProfileManager  COMMAND onyx_tests "--test-case=*ProfileManager*")
add_test(NAME OnyxPathUtils       COMMAND onyx_tests "--test-case=*PathUtils*")
add_test(NAME OnyxAppearance      COMMAND onyx_tests "--test-case=*Appearance*")
add_test(NAME OnyxFrameScheduler COMMAND onyx_tests "--test-case=*FrameScheduler*")
add_test(NAME OnyxSessionLog     COMMAND onyx_tests "--test-case=*SessionLog*")

add_test(NAME MinimalViewer_SelfTest
    COMMAND MinimalViewer --selftest $<TARGET_FILE:MinimalViewer>)
set_tests_properties(MinimalViewer_SelfTest PROPERTIES
    PASS_REGULAR_EXPRESSION "selftest OK")
