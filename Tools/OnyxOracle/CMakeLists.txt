# -- OnyxOracle -- headless Vulkan reference renderer (oracle corpus) -------
# Renders scenes off-screen through Onyx::RenderVk and writes PNGs checked
# against the frozen GL goldens (Tests/Golden/corpus, produced before
# Task 11 deleted the GL renderer that made them -- kept as the permanent
# parity anchor VkOracleParity below still compares against). No UI: links
# only Onyx::RenderVk -- Task 11 proved this standalone (no glfw, no
# AppConfigStub.cpp, no Onyx::Render): see that task's report for the
# link-completeness fixes (ResolveRoleIndices + Camera's AppConfig read)
# that made it possible.
add_executable(onyx-oracle
    Main.cpp
    PngWrite.cpp
    PngRead.cpp
    ImageCompare.cpp
    CorpusTextures.cpp
    CorpusScenes.cpp
    RenderReport.cpp
)
target_include_directories(onyx-oracle PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
target_link_libraries(onyx-oracle PRIVATE Onyx::RenderVk)
onyx_apply_common(onyx-oracle)

# VkBootSmoke -- Init(false)
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

# VkSceneSmoke (T5, extended by T6, T8): builds+renders the M0 blend-stack
# corpus scene through SceneRendererVk twice independently (Solid mode)
# asserting non-clear output and byte-identical reproducibility, repeats
# both checks for a sphere-grid-textured render (Textured/PBR mode), then
# for skinned-cube (plus a skinning-disabled re-render asserting the
# palette actually deforms pixels) and joint-chain-200 (200 single-joint
# batches), and exercises RenderBackground once. T8 adds two RenderContext
# (Include/Onyx/RenderVk/RenderContext.h) pass checks against the
# blend-stack scene: (a) a registered pass tints a 16x16 corner via
# vkCmdClearAttachments -- readback proves the corner differs from a
# no-pass render and everything else stays byte-identical; (b) a pass that
# throws std::runtime_error is caught/logged/skipped (spec §7.1) while a
# second registered pass still runs (its own visible corner tint) and zero
# validation messages are captured. SKIP (77) when no Vulkan-capable
# device/driver is present, same as every other Vk* test above.
add_test(NAME VkSceneSmoke COMMAND onyx-oracle --vk-scene-smoke)
set_tests_properties(VkSceneSmoke PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")

# OracleReproducible: renders the corpus twice (WORK/a, WORK/b) through the
# Vulkan renderer and verifies the two runs are byte-identical (`verify`,
# the same byte-exact comparator this test used against the GL renderer
# before Task 11 deleted it) -- see ReproTest.cmake for the exit-77 (no
# Vulkan-capable device/driver) propagation this depends on, and for why
# "render twice, diff exact" is a meaningful determinism check even though
# nothing here compares against a golden (T5/T7 already established the
# Vulkan renderer is deterministic on one machine/driver -- see
# VkSceneSmoke above, whose own repeated-render assertions this test
# generalizes to the full corpus).
add_test(NAME OracleReproducible
         COMMAND ${CMAKE_COMMAND}
           -DORACLE=$<TARGET_FILE:onyx-oracle>
           -DWORK=${CMAKE_CURRENT_BINARY_DIR}/oracle-repro
           -P ${CMAKE_CURRENT_SOURCE_DIR}/ReproTest.cmake)
set_tests_properties(OracleReproducible PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")

# OracleMatchesGolden (byte-exact fresh-GL-render-vs-golden) died with the
# GL renderer at Task 11 -- there is no GL render left to byte-compare
# against Tests/Golden/corpus. VkOracleParity below is now the sole
# comparator against those frozen goldens, deliberately tolerant (a
# different rasterizer, never byte-exact -- see its own four-knob comment).

# VkOracleParity (T7, "the milestone's teeth"): renders the 5-scene corpus
# through the Vulkan path (render-corpus --renderer vk) and `compare`s it
# against the FROZEN GL goldens (Tests/Golden/corpus -- produced by the GL
# renderer before Task 11 deleted it, never regenerated since; kept as the
# permanent parity anchor).
#
# FOUR-KNOB GATE (T7 fix round, adjudicated): T7's original BLOCKED report
# measured every corpus scene needing maxChannelDelta in the 60-121 range
# to pass a single-pair (maxChannelDelta, differingPct) tolerance -- far
# past that task's own STOP ceiling of 8. Adjudication (opus) POSITIVELY
# identified the cause as 4x-MSAA coverage-quantization noise: worst-pixel
# deltas land on exact k/4 fractions of each edge's local contrast, and
# 100% of delta>8 pixels are edge-adjacent in all 5 scenes (ImageMagick
# `compare -metric AE` diff heatmaps -- see task-7-report.md). A single
# (maxChannelDelta, differingPct) pair cannot separate that expected,
# harmless signature from a real whole-image regression (gamma/lighting/
# mip-filter drift), so the gate is redefined onto four independent tiers
# (Tools/OnyxOracle/ImageCompare.h has the full reasoning per tier):
#   maxChannelDelta   hard-cap tripwire only, deliberately weak (coverage-
#                     only deltas are bounded by local contrast, not
#                     semantics -- a k=2/4-covered sample on a 255-
#                     magnitude edge reads ~127, k=3/4 reads ~191)
#   differingPct      fraction of pixels with ANY nonzero delta
#   highDeltaPct       fraction of pixels with ANY channel delta > 8 --
#                      isolates "a few pixels differ a lot" edge noise
#   mae                per-channel mean absolute error, whole image, max
#                      across channels -- "1.0" reads as one LSB of
#                      uniform whole-image drift; catches what the two
#                      percentage tiers structurally cannot
#
# Values measured on an AMD Radeon RX 6750 XT (Debug, MSVC/Ninja), stable
# across 3 consecutive runs (identical pixelHash every time -- a
# deterministic renderer characteristic, not flaky noise). ALL FOUR VALUES
# ARE SINGLE-GPU POINT ESTIMATES: this milestone has been tuned and run on
# exactly one machine/driver combination so far. Run-to-run variance on
# THIS machine is zero, but MSAA sample positions are driver/hardware
# implementation-defined, so a different GPU vendor (notably lavapipe, the
# software Vulkan implementation CI is expected to use once Vulkan CI
# lands) may need different values on every one of these four knobs -- the
# first lavapipe CI run of VkOracleParity is a TUNING event for these
# cache variables, not a pass/fail verdict on the renderer itself. See
# task-7-report.md for the full per-scene numbers.
set(ONYX_VK_PARITY_MAX_CHANNEL_DELTA 160 CACHE STRING
    "VkOracleParity: hard-cap tripwire on the largest per-channel |a-b| against the GL goldens (weak by design -- see this block's comment)")
set(ONYX_VK_PARITY_MAX_DIFFERING_PCT 2.0 CACHE STRING
    "VkOracleParity: max fraction (%) of pixels allowed any nonzero delta against the GL goldens")
set(ONYX_VK_PARITY_MAX_HIGH_DELTA_PCT 0.8 CACHE STRING
    "VkOracleParity: max fraction (%) of pixels allowed a channel delta > 8 against the GL goldens")
set(ONYX_VK_PARITY_MAX_MAE 1.0 CACHE STRING
    "VkOracleParity: max per-channel mean absolute error (whole image, max across channels) against the GL goldens")
add_test(NAME VkOracleParity
         COMMAND ${CMAKE_COMMAND}
           -DORACLE=$<TARGET_FILE:onyx-oracle>
           -DWORK=${CMAKE_CURRENT_BINARY_DIR}/oracle-vk-parity
           -DGOLDEN=${CMAKE_SOURCE_DIR}/Tests/Golden/corpus
           -DMAX_CHANNEL_DELTA=${ONYX_VK_PARITY_MAX_CHANNEL_DELTA}
           -DMAX_DIFFERING_PCT=${ONYX_VK_PARITY_MAX_DIFFERING_PCT}
           -DMAX_HIGH_DELTA_PCT=${ONYX_VK_PARITY_MAX_HIGH_DELTA_PCT}
           -DMAX_MAE=${ONYX_VK_PARITY_MAX_MAE}
           -P ${CMAKE_CURRENT_SOURCE_DIR}/VkParityTest.cmake)
set_tests_properties(VkOracleParity PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")
