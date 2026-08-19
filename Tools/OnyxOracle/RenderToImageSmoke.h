#pragma once

// Declared in its own header/TU (not Main.cpp) specifically so this smoke
// check's own .cpp can honor task-7-brief.md's Step 1 requirement: "the
// test itself must not include volk or touch any Vulkan type." Main.cpp
// includes plenty of Vulkan headers for its OTHER modes; this file's
// implementation includes none -- see RenderToImageSmoke.cpp's own top
// comment.

namespace Onyx::OracleTool {

/// `onyx-oracle --render-to-image-smoke`: renders the corpus blend-stack
/// scene through Onyx::Rendering::RenderToImage's one-shot overload twice,
/// independently, asserting (a) the result is not a uniform image (real
/// geometry rasterized, not just a flat clear/background) and (b) the two
/// runs are byte-identical -- the same "twice, byte-identical" shape every
/// other Vk* corpus smoke check in this tool uses (see RunVkSceneSmoke's
/// RenderSceneTwice), but through the ready floor's own entry point
/// instead of the raw VkContext/Pipelines/OffscreenTarget/SceneRendererVk
/// sequence those checks hand-roll. This is the proof that entry point
/// needs no Vulkan knowledge to use: this whole check is written without
/// including a single Vulkan header. Exit 0 on success, 1 on any assertion
/// failure, 77 if no Vulkan-capable device/driver is found (SKIP, not
/// FAIL -- same convention as every other Vk* mode in this tool).
int RunRenderToImageSmoke();

} // namespace Onyx::OracleTool
