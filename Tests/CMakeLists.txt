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
    settings_test.cpp
    types_registry_test.cpp
    recentfiles_test.cpp
    assetvisibility_test.cpp
    pathutils_test.cpp
    appearance_test.cpp
    framescheduler_test.cpp
    sessionlog_test.cpp
    typeregistrar_test.cpp
    diagnostics_test.cpp
    jobs_test.cpp
    eventbus_test.cpp
    probe_test.cpp
    workspace_test.cpp
    mounts_test.cpp
    selection_test.cpp
    decoderregistry_test.cpp
    viewerrouting_test.cpp
    documentwindow_test.cpp
    onyxbox_test.cpp
    materialdesc_test.cpp
    cli_test.cpp
    statusbarformat_test.cpp
    infotabfilter_test.cpp
    oracle_corpus_test.cpp
    rendervk_test.cpp
    ${CMAKE_SOURCE_DIR}/Tools/OnyxOracle/CorpusTextures.cpp
    ${CMAKE_SOURCE_DIR}/Tools/OnyxOracle/CorpusScenes.cpp
    ${CMAKE_SOURCE_DIR}/Tools/OnyxOracle/RenderReport.cpp
    ${CMAKE_SOURCE_DIR}/Tools/OnyxOracle/ImageCompare.cpp
)
target_link_libraries(onyx_tests PRIVATE Onyx::Onyx Onyx::Render Onyx_ExampleBox doctest::doctest imgui_lib)
target_include_directories(onyx_tests PRIVATE ${CMAKE_SOURCE_DIR}/Tools/OnyxOracle)
# rendervk_test.cpp is pure (no VkContext/device touched) but needs the
# generated *_spv.h headers on its include path -- linking Onyx::Render
# brings that PUBLIC include dir transitively (onyx_add_spirv,
# cmake/ShaderCompile.cmake); this add_dependencies is the explicit
# build-order guarantee that the header-generation custom target actually
# runs before onyx_tests' own sources compile (target_link_libraries alone
# only orders the link step, not every dependent's compile step).
add_dependencies(onyx_tests Onyx_Render_spirv)
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
add_test(NAME OnyxSettings COMMAND onyx_tests "--test-case=*Settings*")
add_test(NAME OnyxProbe           COMMAND onyx_tests "--test-case=*picks the highest*,*tie at the top*,*below the floor*,*File-path overload*,*A throwing Probe is contained*,*unreadable path*")
add_test(NAME OnyxTypeRegistry    COMMAND onyx_tests "--test-case=*TypeCatalog*,*TypeRegistry*,*TypeRegistrar*")
add_test(NAME OnyxRecentFiles     COMMAND onyx_tests "--test-case=*RecentFiles*")
add_test(NAME OnyxAssetVisibility COMMAND onyx_tests "--test-case=*AssetVisibility*")
add_test(NAME OnyxPathUtils       COMMAND onyx_tests "--test-case=*PathUtils*")
add_test(NAME OnyxAppearance      COMMAND onyx_tests "--test-case=*Appearance*")
add_test(NAME OnyxFrameScheduler COMMAND onyx_tests "--test-case=*FrameScheduler*")
add_test(NAME OnyxSessionLog     COMMAND onyx_tests "--test-case=*SessionLog*")
add_test(NAME OnyxDiagSink       COMMAND onyx_tests "--test-case=*DiagSink*")
add_test(NAME OnyxJobs           COMMAND onyx_tests "--test-case=*JobQueue*,*Cancellation*,*Progress snapshots*,*throwing Work*")
add_test(NAME OnyxEventBus       COMMAND onyx_tests "--test-case=*EventBus*,*Subscription*,*Distinct event types do not cross*,*Cross-thread Post is safe*")
add_test(NAME OnyxWorkspace       COMMAND onyx_tests "--test-case=*Workspace opens a document*,*No module accepts the file*,*OpenAsync delivers TreeReady*,*Close posts DocumentClosed*,*FindModule resolves by id then hint*,*A single registered module skips the probe floor*,*Close during an in-flight OpenAsync parse*,*A throwing ParseContainer is contained*,*AddModule rejects a duplicate module id*,*CancelOpen cancels a blocked async parse*,*SetWorkspaceSettingsPath swaps in Settings*")
add_test(NAME OnyxMounts          COMMAND onyx_tests "--test-case=*MountSpec:*,*Extract: fileIndex 1*,*Extract: an out-of-range fileIndex*,*OnyxBox obxpak:*")
add_test(NAME OnyxSelection       COMMAND onyx_tests "--test-case=*Selection:*")
add_test(NAME OnyxDecoders        COMMAND onyx_tests "--test-case=*DecoderRegistry*")
add_test(NAME OnyxViewerRouting   COMMAND onyx_tests "--test-case=*RouteForType*,*ViewerOpening:*")
add_test(NAME OnyxDocumentWindow  COMMAND onyx_tests "--test-case=*DocumentWindow:*")
add_test(NAME OnyxBox            COMMAND onyx_tests "--test-case=*onyxbox end to end*,*onyxbox types are minted*,*onyxbox clamps a lying TOC count*,*onyxbox rejects an image whose payloadSize*,*onyxbox decodes a mesh entry*,*onyxbox flags a mesh entry Failed*")
add_test(NAME OnyxMaterialDesc    COMMAND onyx_tests "--test-case=*ResolveRoleIndices*")
add_test(NAME OnyxCli             COMMAND onyx_tests "--test-case=*cli probe prints a table*,*cli list json contains type key*,*cli extract writes payloadSize*,*cli extract skips unsafe entry names*,*cli decode text entry prints hello box*,*cli decode strict returns kStrictErrors*,*cli decode scene entry prints parts materials vertices summary*,*cli decode unknown entry name*,*cli list returns kNoModule*,*cli Run dispatches probe list extract*,*cli Run returns kUsage to err*,*cli probe returns kNoModule when the ranking*,*cli decode returns kOk when a capability exists*,*cli Run --game hint overrides ranking*,*cli parse keeps a 0xFFFFFFFF*")
add_test(NAME OnyxStatusBarFormat COMMAND onyx_tests "--test-case=*StatusBarFormat:*")
add_test(NAME OnyxInfoTabFilter   COMMAND onyx_tests "--test-case=*InfoTabFilter:*")
add_test(NAME OnyxOracleCorpus    COMMAND onyx_tests "--test-case=*OracleCorpus:*")
add_test(NAME OnyxRenderVkSpirv   COMMAND onyx_tests "--test-case=*RenderVkSpirv:*")
add_test(NAME OnyxRenderContext   COMMAND onyx_tests "--test-case=*RenderContext:*")
add_test(NAME OnyxDeferredDestroyQueue COMMAND onyx_tests "--test-case=*DeferredDestroyQueue:*")

add_test(NAME MinimalViewer_SelfTest
    COMMAND MinimalViewer --selftest $<TARGET_FILE:MinimalViewer>)
set_tests_properties(MinimalViewer_SelfTest PROPERTIES
    PASS_REGULAR_EXPRESSION "selftest OK")
