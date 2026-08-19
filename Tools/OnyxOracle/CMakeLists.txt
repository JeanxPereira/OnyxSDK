# -- OnyxOracle -- headless GL reference renderer (M0 oracle corpus) --------
# Renders scenes off-screen through the real Onyx::Render shaders and writes
# PNGs the future Vulkan renderer must match pixel-for-pixel. No UI: links
# only Onyx::Render + glfw, the same composition-root exception Examples/
# already takes.

add_executable(onyx-oracle
    Main.cpp
    HeadlessGL.cpp
    AppConfigStub.cpp
    CorpusTextures.cpp
    # Tasks 3-4 append CorpusScenes.cpp, RenderReport.cpp
)
target_include_directories(onyx-oracle PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
target_link_libraries(onyx-oracle PRIVATE Onyx::Render glfw)
onyx_apply_common(onyx-oracle)

add_test(NAME OracleGlSmoke COMMAND onyx-oracle --gl-smoke)
set_tests_properties(OracleGlSmoke PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")
