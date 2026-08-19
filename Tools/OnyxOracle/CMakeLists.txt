# -- OnyxOracle -- headless GL reference renderer (M0 oracle corpus) --------
# Renders scenes off-screen through the real Onyx::Render shaders and writes
# PNGs the future Vulkan renderer must match pixel-for-pixel. No UI: links
# only Onyx::Render + glfw, the same composition-root exception Examples/
# already takes.

add_executable(onyx-oracle
    Main.cpp
    HeadlessGL.cpp
    PngWrite.cpp
    AppConfigStub.cpp
    CorpusTextures.cpp
    CorpusScenes.cpp
    RenderReport.cpp
)
target_include_directories(onyx-oracle PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
target_link_libraries(onyx-oracle PRIVATE Onyx::Render Onyx::RenderVk glfw)
onyx_apply_common(onyx-oracle)

add_test(NAME OracleGlSmoke COMMAND onyx-oracle --gl-smoke)
set_tests_properties(OracleGlSmoke PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")

# VkBootSmoke: mirrors OracleGlSmoke for the Vulkan boot path -- Init(false)
# (headless, no swapchain), print the picked device name, Shutdown, exit 0.
# Exit 77 (SKIP, not FAIL) when no Vulkan-capable device/driver is present.
add_test(NAME VkBootSmoke COMMAND onyx-oracle --vk-smoke)
set_tests_properties(VkBootSmoke PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")

# VkValidationSelfTest (T2-review rider #2): a Debug-only, deliberate-error
# path proving VkContext's validation-message counter actually fires --
# copies a live Buffer struct, destroys the original, then destroys the
# stale copy (a validation-layer-catchable stale-handle destroy), and
# asserts ValidationMessageCount() > 0. Isolated in its own CLI mode so
# VkBootSmoke's normal "zero validation messages" assertion is never
# touched by this deliberately-broken path. SKIP (77) on a Release build
# or wherever the validation layer isn't available -- nothing to
# self-test there.
add_test(NAME VkValidationSelfTest COMMAND onyx-oracle --vk-validation-selftest)
set_tests_properties(VkValidationSelfTest PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")

# VkSceneSmoke (T5, extended by T6): builds+renders the M0 blend-stack
# corpus scene through SceneRendererVk twice independently (Solid mode)
# asserting non-clear output and byte-identical reproducibility, repeats
# both checks for a sphere-grid-textured render (Textured/PBR mode), then
# for skinned-cube (plus a skinning-disabled re-render asserting the
# palette actually deforms pixels) and joint-chain-200 (200 single-joint
# batches), and exercises RenderBackground once. SKIP (77) when no
# Vulkan-capable device/driver is present, same as every other Vk* test
# above.
add_test(NAME VkSceneSmoke COMMAND onyx-oracle --vk-scene-smoke)
set_tests_properties(VkSceneSmoke PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")

# OracleReproducible: renders the corpus twice (WORK/a, WORK/b) and verifies
# the two runs are byte-identical -- see ReproTest.cmake for the exit-77
# (no display session) propagation this depends on.
add_test(NAME OracleReproducible
         COMMAND ${CMAKE_COMMAND}
           -DORACLE=$<TARGET_FILE:onyx-oracle>
           -DWORK=${CMAKE_CURRENT_BINARY_DIR}/oracle-repro
           -P ${CMAKE_CURRENT_SOURCE_DIR}/ReproTest.cmake)
set_tests_properties(OracleReproducible PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")

# OracleMatchesGolden: the gate -- compares a fresh render-corpus run
# (produced by OracleReproducible, reused rather than re-rendered) against
# the committed reference corpus in Tests/Golden/corpus.
add_test(NAME OracleMatchesGolden
         COMMAND onyx-oracle verify ${CMAKE_SOURCE_DIR}/Tests/Golden/corpus
                 ${CMAKE_CURRENT_BINARY_DIR}/oracle-repro/a)
set_tests_properties(OracleMatchesGolden PROPERTIES DEPENDS OracleReproducible
         SKIP_RETURN_CODE 77 LABELS "render")
