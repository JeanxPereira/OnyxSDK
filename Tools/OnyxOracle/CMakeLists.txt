# -- OnyxOracle -- headless GL reference renderer (M0 oracle corpus) --------
# Renders scenes off-screen through the real Onyx::Render shaders and writes
# PNGs the future Vulkan renderer must match pixel-for-pixel. No UI: links
# only Onyx::Render + glfw, the same composition-root exception Examples/
# already takes.

add_executable(onyx-oracle
    Main.cpp
    HeadlessGL.cpp
    PngWrite.cpp
    PngRead.cpp
    ImageCompare.cpp
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

# VkOracleParity (T7, "the milestone's teeth"): renders the 5-scene corpus
# through the Vulkan path (render-corpus --renderer vk) and `compare`s it
# against the FROZEN GL goldens (Tests/Golden/corpus -- never regenerated;
# see OracleMatchesGolden above, which pins the GL side byte-exact).
#
# STATUS: BLOCKED, not tuned-and-green. Measured on an AMD Radeon RX 6750
# XT (Debug, MSVC/Ninja), stable across 3 consecutive runs (identical
# pixelHash every time -- this is a deterministic renderer characteristic,
# not flaky noise): after the task-7 mip remedy (Resources::CreateImage2D
# generateMips + sampler maxLod, see VkResources.h), every one of the 5
# scenes still needs maxChannelDelta in the 60-121 range to pass -- far
# past the task-7 brief's own STOP ceiling of 8. Per-pixel diff heatmaps
# (ImageMagick `compare -metric AE`) show the divergence is confined
# EXCLUSIVELY to a 1-2px band along silhouette/geometric edges (sphere
# rims, box outlines, the joint-chain-200 loop's contour) -- interior
# shading, texturing, skinning, and blending are pixel-identical. This is
# the signature of GL and Vulkan drivers using different, implementation-
# defined 4x MSAA sample positions for the same triangle edges -- expected
# cross-API rasterization noise, not a lighting/blend/pose bug -- but the
# brief's STOP condition instructs reporting BLOCKED with evidence rather
# than tuning past it, so these two values are pinned at the brief's own
# ceiling (8, 2.0) -- deliberately NOT the ~121/1.5 values that would
# actually make this test pass, which would defeat the point of the gate.
# See task-7-report.md for the full per-scene numbers and diff evidence;
# resolving this (VK_EXT_sample_locations to match GL's sample pattern, a
# silhouette-aware compare mode, or an owner decision to widen these two
# numbers with eyes open) is left to the milestone owner.
set(ONYX_VK_PARITY_MAX_CHANNEL_DELTA 8 CACHE STRING
    "VkOracleParity: max per-channel |a-b| tolerated against the GL goldens (task 7 BLOCKED -- see task-7-report.md)")
set(ONYX_VK_PARITY_MAX_DIFFERING_PCT 2.0 CACHE STRING
    "VkOracleParity: max fraction (%) of pixels allowed any nonzero delta against the GL goldens (task 7 BLOCKED -- see task-7-report.md)")
add_test(NAME VkOracleParity
         COMMAND ${CMAKE_COMMAND}
           -DORACLE=$<TARGET_FILE:onyx-oracle>
           -DWORK=${CMAKE_CURRENT_BINARY_DIR}/oracle-vk-parity
           -DGOLDEN=${CMAKE_SOURCE_DIR}/Tests/Golden/corpus
           -DMAX_CHANNEL_DELTA=${ONYX_VK_PARITY_MAX_CHANNEL_DELTA}
           -DMAX_DIFFERING_PCT=${ONYX_VK_PARITY_MAX_DIFFERING_PCT}
           -P ${CMAKE_CURRENT_SOURCE_DIR}/VkParityTest.cmake)
set_tests_properties(VkOracleParity PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")
