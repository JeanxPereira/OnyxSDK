#pragma once

// ═══════════════════════════════════════════════════════════════════════
// Onyx::Rendering::RenderToImage -- the ready floor's top step (M4 debt
// I5; M5 Task 7, docs/superpowers/plans/2026-08-19-onyx-v1-m5-generality.md
// #7). Spec §2's ready-floor promise ("a complete, opinionated path that
// covers the common case in a few lines") named SceneRenderer generically,
// but until this task every entry point that actually renders a
// Parsers::SceneData through the Vulkan renderer
// (Onyx::Rendering::SceneRendererVk) took a caller-supplied VkContext&, a
// VkCommandBuffer already open inside a dynamic-rendering pass, and a
// caller-created ScenePipelines/OffscreenTarget pair -- the RAW floor,
// correctly documented as such (Include/Onyx/RenderVk/*.h), with no ready
// floor above it. All three in-tree consumers that just want "a
// Parsers::SceneData in, an RGBA image out" (Source/Cli/Render.cpp,
// Tools/OnyxOracle/Main.cpp's render-corpus loop, and -- see the note
// below -- Source/Viewers/Viewport3D.cpp) had independently re-implemented
// the same call sequence: VkContext::Init, Pipelines::CreateScene[+
// CreateBackground], OffscreenTarget::Create, SceneRendererVk::Build, a
// Resources::OneShot command buffer recording BeginFrame/[RenderBackground
// /]Render/EndFrame, OffscreenTarget::Readback, then unwinding all of it in
// reverse -- task-7-brief.md's own words, "the same ~60 lines". This is
// that missing step.
//
// ── the projection-convention decision (BINDING; read before calling) ──
// RenderRequest::proj is a PLAIN projection matrix -- e.g. straight out of
// glm::perspective() (which, thanks to GLM_FORCE_DEPTH_ZERO_TO_ONE being
// PUBLIC on the Onyx_Render CMake target -- see Include/Onyx/RenderVk/
// Pipelines.h's "Camera convention" note -- already carries Vulkan's [0,1]
// clip depth for any TU that links Onyx::Render). Do NOT pre-apply
// Onyx::Rendering::VulkanProjection() to it yourself: RenderToImage applies
// that Y-flip internally, exactly once, immediately before handing the
// matrix to SceneRendererVk::Render(). This is the "total contract" shape
// the M4 final review argued for, over "take an already-converted proj and
// trust the caller remembered": the failure mode a partial contract leaves
// open is not hypothetical -- Tools/OnyxOracle/Main.cpp forgot the flip for
// an entire milestone before T7's pixel-vs-golden comparison caught it
// (Pipelines.h's own incident writeup). A caller of THIS entry point cannot
// reproduce that failure by omission, because there is no separate flip
// step left to omit. The residual failure mode -- a caller pre-flips
// anyway, so RenderToImage's own internal flip cancels it back to GL
// convention -- is caught immediately in Debug builds, not silently: this
// header's implementation asserts request.proj still looks unconverted
// (proj[1][1] > 0) on entry, and SceneRendererVk::Render() itself
// separately asserts the opposite (proj[1][1] < 0) on the matrix
// RenderToImage hands it downstream -- either assert firing means a caller
// touched a convention this API exists specifically so they never have to.
//
// ── background (disclosed, deliberate extension beyond the brief's literal
// three-field RenderRequest sketch) ──
// RenderRequest carries hasBackground/backgroundTop/backgroundBottom
// beyond the {scene, width, height, view, proj, mode} the task brief's own
// code snippet shows. Reason: Tools/OnyxOracle/Main.cpp's render-corpus
// loop -- explicitly named as one of the three consumers this task
// refactors, and gated by a BINDING byte-identical-goldens requirement
// (task-7-brief.md's gate: "if a pixel moves, STOP and report BLOCKED") --
// renders a top/bottom gradient background before the scene on every
// corpus frame; that background is not incidental, it is most of what a
// corpus PNG's non-geometry pixels show. RenderToImage owns the entire
// command-buffer/frame lifecycle internally (that ownership is the whole
// point of the API), so there is no seam left for a caller to inject an
// extra draw before the scene the way a raw-floor caller can -- without a
// background field, the oracle's render-corpus loop could not move onto
// this entry point at all without silently dropping its background pass
// and breaking the golden-parity gate. Left unset (the default), a request
// produces exactly the flat-clear-color frame Source/Cli/Render.cpp always
// has -- zero added complexity for that consumer, or any future one that
// doesn't need a background.
//
// ── Source/Viewers/Viewport3D.cpp: deliberately NOT routed through this
// API ──
// Viewport3D::RenderFrame renders into a PERSISTENT OffscreenTarget it
// owns across many frames and exposes live (never read back to the CPU) as
// an ImGui ImTextureID via TexturePool::RegisterExternalView -- and, on
// top of the scene itself, layers a background gradient, an optional
// skeleton/gizmo overlay, and an optional world-space grid, each through
// its own long-lived pipeline object built once in EnsureVulkanReady() and
// reused every redraw. RenderToImage's contract -- own a target for
// exactly one call, read it back to `rgbaOut`, tear everything down -- is
// structurally incompatible with that: routing RenderFrame through it
// would rebuild the scene pipeline/renderer/target from scratch on every
// single interactive redraw (this class already carries one disclosed
// perf gap, a blocking OneShot submit every redraw -- see CHANGELOG's "Known
// gaps" -- compounding it with full resource churn is not a fix), drop the
// background/skeleton/grid passes entirely (no field for them, deliberately
// -- see the background note above on why even ONE extra pass already
// costs a field, and Viewport3D needs three), and would need new,
// currently-nonexistent TexturePool machinery to re-upload a CPU RGBA
// buffer back into a live, sampled GPU image every frame -- strictly worse
// than what SceneRendererVk::Render() already draws directly into the
// resolve target it owns. This is exactly the case spec §2 names
// explicitly: "the ready floor never hides the raw floor -- if a toolkit
// outgrows a stock path, it drops one floor without leaving the SDK."
// Viewport3D's live, multi-pass, GPU-resident viewport is that toolkit;
// it stays on the raw floor (VkContext + Pipelines + SceneRendererVk +
// OffscreenTarget directly, unchanged by this task).
// ═══════════════════════════════════════════════════════════════════════

#include <Onyx/Rendering/RenderBatch.h> // Rendering::ShadingMode, transitively Parsers::SceneData

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Onyx::Rendering {

// Forward-declared only -- never include VkContext.h/volk.h from this
// header. See Include/Onyx/Viewers/Viewport3D.h's own top comment for why
// a header this easy to reach for (any headless render caller) must not
// transitively pull in <windows.h> via VK_USE_PLATFORM_WIN32_KHR. A
// reference to an incomplete type needs no definition here -- only
// Source/Rendering/RenderToImage.cpp (compiled into Onyx_Render, already
// Vulkan-aware) needs the real VkContext.h.
class VkContext;

/// Everything one scene render needs. `scene` is borrowed for the
/// duration of the call only -- neither RenderToImage overload keeps a
/// reference to it past return.
struct RenderRequest {
    const Parsers::SceneData& scene;
    int width = 0;
    int height = 0;
    glm::mat4 view{1.0f};

    /// PLAIN (non-Vulkan-converted) projection matrix -- see this header's
    /// top comment for the full contract. Do not call
    /// Onyx::Rendering::VulkanProjection() on this yourself.
    glm::mat4 proj{1.0f};

    Rendering::ShadingMode mode = Rendering::ShadingMode::Solid;

    /// Optional top/bottom gradient background, rendered before the scene
    /// (SceneRendererVk::RenderBackground's own contract). backgroundTop/
    /// backgroundBottom are ignored unless hasBackground is true -- see
    /// this header's top comment for why this field exists at all.
    bool hasBackground = false;
    glm::vec3 backgroundTop{0.0f};
    glm::vec3 backgroundBottom{0.0f};

    /// Frame clear color, underneath the scene/background. Irrelevant
    /// whenever hasBackground is true (RenderBackground's fullscreen
    /// triangle -- no depth test, no blend -- always fully overdraws it).
    /// Defaults to Source/Cli/Render.cpp's own pre-existing neutral gray.
    glm::vec4 clearColor{0.10f, 0.11f, 0.13f, 1.0f};
};

/// One-off callers: owns a VkContext's entire lifetime (Init through
/// Shutdown) plus every pipeline/target/renderer this render needs,
/// start to finish, internally. No Vulkan type appears anywhere in this
/// overload's signature -- a caller needs only this header plus
/// <vector>/<string>/<glm/glm.hpp>, never links volk, never names a single
/// Vk* type. Returns tightly packed top-down RGBA (request.width *
/// request.height * 4 bytes) in `rgbaOut` on success.
///
/// On failure to find a Vulkan-capable device/driver, `err` is set to the
/// literal prefix "no Vulkan device: " followed by the underlying reason --
/// a plain string convention (not an error code/enum) deliberately usable
/// without touching any Vulkan type, so a headless caller can tell "no
/// device, treat as SKIP" apart from "a real render failure" without
/// including a single Vulkan header. See Tools/OnyxOracle/
/// RenderToImageSmoke.cpp for exactly that pattern -- it is the proof this
/// overload needs no Vulkan type at all.
bool RenderToImage(const RenderRequest& request, std::vector<uint8_t>& rgbaOut, std::string& err);

/// Repeated-render callers (Tools/OnyxOracle/Main.cpp's render-corpus
/// loop, which renders 5 scenes through one already-booted VkContext):
/// reuses `ctx` (must already be Init()'d by the caller; this function
/// never calls Shutdown() on it) but still owns the pipeline/target/
/// renderer lifetime for THIS call only -- each call creates and destroys
/// its own ScenePipelines[+BackgroundPipeline]/OffscreenTarget/
/// SceneRendererVk, nothing is cached across calls. A caller that needs to
/// amortize pipeline creation across many renders of a live, multi-pass
/// target belongs on the raw floor instead -- see this header's top
/// comment (the Viewport3D note) for exactly that case and why it stays
/// there.
bool RenderToImage(VkContext& ctx, const RenderRequest& request, std::vector<uint8_t>& rgbaOut,
                    std::string& err);

} // namespace Onyx::Rendering
