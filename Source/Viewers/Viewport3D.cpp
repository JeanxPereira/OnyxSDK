// ═══════════════════════════════════════════════════════════════════════
// T10 port notes (Shell swap part 2 -- see task-10-report.md for the full
// writeup; this comment covers the decisions load-bearing enough to want
// beside the code they explain).
//
// VulkanState / header split: Viewport3D.h forward-declares every Vulkan-
// touching type and hides them behind this file's private `VulkanState`
// struct (unique_ptr<incomplete-type>, exactly Include/Onyx/App/Window.h's
// own pattern) rather than including OffscreenTarget.h/Pipelines.h/
// SceneRendererVk.h directly in the header. This is not stylistic: those
// headers pull in volk.h, which on Windows pulls in <windows.h> for
// VK_USE_PLATFORM_WIN32_KHR -- and Viewport3D.h is transitively included
// by DocumentBrowser.cpp and CameraPanel.cpp, neither of which has
// anything to do with Vulkan. The first version of this port included
// them directly and broke the build: <wingdi.h>'s `#define DecodedText
// TextOutW` (or `TextOutA`) collided with `Onyx::Modules::DecodedText`, a real
// type DocumentBrowser.cpp names in its own ViewerOpener wiring.
//
// Recording seam: T9's Window.cpp records the swapchain frame's own
// command buffer (m_vk->commandBuffers[...]) already inside an open
// dynamic-rendering scope over the SWAPCHAIN image by the time
// RenderContext::Execute runs (see Window.cpp's frameEnd() doc comment) --
// Vulkan does not allow a second, nested vkCmdBeginRendering on that same
// command buffer, so Viewport3D cannot record its OffscreenTarget's
// BeginFrame/EndFrame through a RenderContext::AddPass callback (that seam
// is for drawing directly onto the swapchain image, not into a second,
// independent render target). Instead, RenderFrame() below uses its own,
// separate command buffer via Onyx::Rendering::Resources::OneShot (the same
// primitive T4/T5/T7's own smoke/oracle paths already use to drive
// OffscreenTarget) -- allocate, record BeginFrame/draws/EndFrame/
// PrepareForSampling, submit, and BLOCK until the GPU finishes, all inside
// one OneShot call. This only runs when m_needsRedraw is set (camera
// moved, a scene just loaded, a toggle changed) rather than every single
// ImGui frame, so the blocking cost is bounded to actual redraws -- the
// same "cache the FBO, only re-render on change" strategy the GL version
// used, just with a synchronous GPU round trip standing in for GL's
// implicit "draw commands queue against the current context" model. A
// truly async, frame-pipelined recording path (reusing T9's own frames-in-
// flight machinery for a second target) is a reasonable follow-up but is
// not what this task's file list or time budget covers.
//
// No scene decoder exists in OnyxBox yet (T14 gap, stated in the task
// brief): LoadScene() is exercised by nothing today, so this whole path
// compiles and initializes correctly but is NOT visually verified by any
// test in this milestone -- MinimalViewer's --open-first-image proof
// (Examples/MinimalViewer) opens an IMAGE entry, not a scene, precisely
// because no scene entry exists to open. Stated here, honestly, rather
// than implied.
//
// Feature gaps inherited from SceneRendererVk (Include/Onyx/Rendering/
// SceneRendererVk.h), not introduced by this task -- do not "fix" these
// here, per the brief's "MSAA + outline parity comes from the renderer --
// do not reimplement effects in the viewer":
//   - [Milestone T6 update: SceneRendererVk gained SetAnimation/
//     UpdateAnimation/StopAnimation/GetAnimPlayer (Task 3, then Task 5's
//     host-writable joint palette SSBO) -- the line that used to be here
//     ("no animation playback API exists on SceneRendererVk this
//     milestone") is no longer true. The GL path's transport bar, clip
//     browser, and per-frame UpdateAnimation() are restored below (see
//     DrawTransportBar(), DrawInspector()'s clip tree, and Draw()'s
//     animation-update block) -- ported from the pre-Vulkan Viewport3D
//     (git show 71fe575^:Source/Viewers/Viewport3D.cpp), receiver swapped
//     from the deleted GL m_sceneRenderer to m_vk->sceneRendererVk. The
//     "debug disable skin" toggle the GL toolbar also had is NOT restored
//     -- SceneRendererVk has no equivalent knob, and adding one is outside
//     this task's scope.]
//   - Task 5 (M5) closed the isVisible half of this gap: Render() now
//     skips `!isVisible` batches in every pass, so the inspector's
//     visibility checkboxes (which mutate GetBatches() -- the exact same
//     Rendering::RenderBatch struct GL's SceneRenderer also filled -- see
//     SceneRendererVk.h's own "RenderBatch reuse" comment) have a real
//     effect again. isHighlighted stays unread -- no hover-outline pass
//     exists on this renderer, and reading the flag with no pass to show
//     it would be worse than the declared gap (see CHANGELOG's "Known
//     gaps").
//
// Y-flip on display: GL's ImGui::Image() call used uv0=(0,1)/uv1=(1,0) to
// flip vertically (GL's texture origin is bottom-left). Vulkan's resolve
// image is already top-down in memory (OffscreenTarget::Readback's own
// doc comment: "empirically verified top-down for this target") and
// ImGui's own UV convention is top-down too, so the Vulkan path below
// draws with the plain uv0=(0,0)/uv1=(1,1) -- no flip.
//
// M5 Task 7 note: this class deliberately does NOT route RenderFrame()
// through the new Onyx::Rendering::RenderToImage (Include/Onyx/Rendering/
// RenderToImage.h) -- see that header's own top comment ("Source/Viewers/
// Viewport3D.cpp: deliberately NOT routed through this API") for the full
// reasoning. Short version: RenderFrame() renders into a PERSISTENT
// OffscreenTarget this class owns across many frames and samples live via
// ImGui (never reads it back to the CPU), layers a background gradient
// plus optional skeleton/grid passes on top of the scene through pipeline
// objects built once and reused every redraw, and already carries one
// disclosed perf gap (a blocking OneShot submit every redraw). RenderToImage
// owns a target for exactly one call and hands back CPU bytes, which is
// the opposite shape from what a live, multi-pass, GPU-resident viewport
// needs -- spec §2's own principle ("the ready floor never hides the raw
// floor -- if a toolkit outgrows a stock path, it drops one floor without
// leaving the SDK") is why this class stays on the raw floor (VkContext +
// Pipelines + SceneRendererVk + OffscreenTarget directly) below, unchanged
// by that task.
// ═══════════════════════════════════════════════════════════════════════

#include <Onyx/Viewers/Viewport3D.h>
#include <Onyx/App/TexturePool.h>
#include <Onyx/Rendering/OffscreenTarget.h>
#include <Onyx/Rendering/Pipelines.h>
#include <Onyx/Rendering/SceneRendererVk.h>
#include <Onyx/Rendering/VkContext.h>
#include <Onyx/Services/Events.h>
#include <imgui.h>
#include <Onyx/Services/AppConfig.h>
#include <Onyx/Services/Logger.h>
#include <Onyx/Services/ThemeManager.h>
#include <Onyx/Fonts/SFSymbols.h>
#include <Onyx/App/Panels/CameraPanel.h>
#include <Onyx/App/Widgets.h>
#include "App/AnimationTimeline.h"
#include "App/ActiveAnimation.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <limits>

namespace Onyx::Viewers {

using Onyx::Rendering::Resources;
using Onyx::Rendering::VulkanProjection;

// Heights of the animation strip Draw() reserves under the 3D image (fix
// round 1, task-6-report.md). Full height matches the historical GL
// transport bar's constant exactly (two rows: buttons + rich timeline,
// git show 71fe575^:Source/Viewers/Viewport3D.cpp line 131). Selector
// height is new -- DrawClipSelector() is one row (a label + one combo)
// inside a child window with the ImGui default ~8px top/bottom padding, so
// 40px seats one ~24px-tall combo without a visibly empty band beneath it.
constexpr float kAnimTransportHeight = 86.0f;
constexpr float kAnimSelectorHeight  = 40.0f;

// ── VulkanState -- see Viewport3D.h's top comment for why this is a
// forward-declared, .cpp-only struct ─────────────────────────────────────
struct Viewport3D::VulkanState {
    Onyx::Rendering::ScenePipelines     scenePipelines;
    Onyx::Rendering::GridPipeline       gridPipeline;
    Onyx::Rendering::BackgroundPipeline backgroundPipeline;
    Onyx::Rendering::OverlayPipeline    overlayPipeline;
    Onyx::Rendering::SceneRendererVk    sceneRendererVk;
    Onyx::Rendering::OffscreenTarget    target;
    bool targetCreated = false;
};

Viewport3D::Viewport3D(const std::string& name) : m_name(name) {
    m_vk = std::make_unique<VulkanState>();

    // Task 11: Onyx::Rendering::Camera's constructor used to pull these
    // same fields from AppConfig itself (see Camera.cpp's own comment for
    // why that moved) -- applying them here, right after m_camera's default
    // construction, keeps the "new viewport matches the user's last-session
    // camera preferences" behavior exactly, just from the one Shell class
    // that owns both AppConfig and the Camera instance. Mirrors
    // Source/App/Panels/CameraPanel.cpp's own field list, which writes
    // these same members back into AppConfig when the user edits them.
    if (auto* cfg = Onyx::Services::AppConfig::Get()) {
        m_camera.fov               = cfg->camFov;
        m_camera.nearPlane         = cfg->camNearPlane;
        m_camera.farPlane          = cfg->camFarPlane;
        m_camera.autoNear          = cfg->camAutoNear;
        m_camera.autoFar           = cfg->camAutoFar;
        m_camera.manualNear        = cfg->camManualNear;
        m_camera.manualFar         = cfg->camManualFar;
        m_camera.nearDistanceScale = cfg->camNearDistanceScale;
        m_camera.farMargin         = cfg->camFarMargin;
        m_camera.nearFarRatioMax   = cfg->camNearFarRatioMax;
    }
}

Viewport3D::~Viewport3D() {
    // Shutdown-order guard (T10 disclosed gap -- see task-10-report.md's
    // Concerns, same rationale as TexturePool's destructor comment): only
    // touch a Vulkan handle if the process-wide accessor still reports the
    // context alive. A full-app shutdown with this document still open
    // reaches this destructor after Window::exitVulkan() has already
    // cleared it -- everything below is deliberately skipped (leaked) in
    // that case rather than risking UB against an already-torn-down
    // device; the process is exiting either way.
    // Recovered from the pre-Vulkan Viewport3D (T6 restoration): clear the
    // cross-panel active-player broker (Dopesheet/AnimCurveView read it) if
    // we're the one who published it -- otherwise those two panels keep a
    // dangling AnimationPlayer* into the SceneRendererVk this destructor is
    // about to tear down. GetAnimPlayer() is a plain pointer read (no
    // Vulkan call), so this runs unconditionally, unlike the guarded block
    // below.
    if (Onyx::App::GetActiveAnimationPlayer() == m_vk->sceneRendererVk.GetAnimPlayer()) {
        Onyx::App::SetActiveAnimationPlayer(nullptr);
    }

    m_texPool.reset(); // safe either way -- TexturePool::~TexturePool() carries the same guard itself

    Onyx::Rendering::VkContext* live = Onyx::Rendering::GetGlobalContext();
    if (live && m_vkReady) {
        m_vk->sceneRendererVk.Clear(*live);
        if (m_vk->targetCreated) m_vk->target.Destroy(*live);
        Onyx::Rendering::Pipelines::Destroy(*live, m_vk->overlayPipeline);
        Onyx::Rendering::Pipelines::Destroy(*live, m_vk->backgroundPipeline);
        Onyx::Rendering::Pipelines::Destroy(*live, m_vk->gridPipeline);
        Onyx::Rendering::Pipelines::Destroy(*live, m_vk->scenePipelines);
    }
}

std::string Viewport3D::GetName() const { return m_name; }

bool Viewport3D::HasBatches() const {
    return !m_vk->sceneRendererVk.GetBatches().empty();
}

void Viewport3D::ClearScene() {
    // Recovered from the pre-Vulkan Viewport3D (T6 restoration): same
    // active-player guard as the destructor, but here it must run BEFORE
    // sceneRendererVk.Clear() below destroys the AnimationPlayer it may be
    // pointing at.
    if (Onyx::App::GetActiveAnimationPlayer() == m_vk->sceneRendererVk.GetAnimPlayer()) {
        Onyx::App::SetActiveAnimationPlayer(nullptr);
    }

    m_sceneData.reset();
    m_bounds = Onyx::Domain::BoundingBox{};
    if (m_vkReady) {
        Onyx::Rendering::VkContext* live = Onyx::Rendering::GetGlobalContext();
        if (live) m_vk->sceneRendererVk.Clear(*live);
    }
    m_needsRedraw = true;
}

void Viewport3D::LoadFromMeshData(const Parsers::MeshData& data,
                                  const std::vector<std::unique_ptr<Parsers::TextureData>>& textures) {
    // Dead API (verified: zero callers anywhere in this repo, including
    // Tools/OnyxOracle and Examples -- grep before this task started).
    // Its old body constructed and drove Onyx::Rendering::SceneRenderer
    // (GL) directly, which is exactly the "GL call with no context
    // current" landmine T9's report flagged and this task exists to close
    // -- rather than port a MeshData -> SceneRendererVk::Build path
    // nothing exercises, this stays a clearly-logged no-op. Left in place
    // (not deleted) only to avoid touching the public header surface
    // beyond what this task's GL-removal scope requires.
    (void)data;
    (void)textures;
    ONYX_LOGF_WARN("[Viewport3D] LoadFromMeshData: not ported to Vulkan (dead code path, no callers) -- ignored");
    ClearScene();
}

void Viewport3D::LoadScene(std::unique_ptr<Parsers::SceneData> scene) {
    ClearScene();
    if (!scene || scene->IsEmpty()) return;

    m_sceneData = std::shared_ptr<Parsers::SceneData>(scene.release());

    if (m_sceneData->animations) {
        EventAnimationLoaded::post(m_sceneData->animations);
    }

    ComputeBounds();
    m_camera.FocusOn(m_bounds);
    m_needsRedraw = true;
    m_lastFrameTime = 0.0f;

    EnsureVulkanReady();
    if (m_vkReady && m_ctx) {
        std::string err;
        if (!m_vk->sceneRendererVk.Build(*m_ctx, m_vk->scenePipelines, *m_sceneData, err)) {
            ONYX_LOGF_ERR("[Viewport3D] SceneRendererVk::Build failed: %s", err.c_str());
        }
    }
}

void Viewport3D::ComputeBounds() {
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(-std::numeric_limits<float>::max());
    bool any = false;
    if (m_sceneData) {
        // T11-review F1: bounds must be computed in RENDERED space, not
        // object space -- the GL SceneRenderer::Build did exactly this
        // (git show 7525d1f:Source/Rendering/SceneRenderer.cpp) and said
        // why: FocusOn's camera framing has to cover every vertex as it
        // actually draws, and SceneRendererVk::Build applies this same
        // instanceTransform (Source/Rendering/SceneRendererVk.cpp, "a
        // per-game bind-pose orientation convention: some source formats
        // author models facing -Z ... others are already screen-correct --
        // identical to GL's SceneRenderer::Build") before rasterizing. Skipping this
        // transform here left ComputeBounds silently out of sync with
        // what the renderer draws: any asset with a non-identity
        // instanceTransform framed on the wrong center, and any GOW2
        // asset (flipZ=true) framed mirrored in Z.
        const glm::mat4 instanceTransform = m_sceneData->flipZ
            ? glm::scale(m_sceneData->instanceTransform, glm::vec3(1.0f, 1.0f, -1.0f))
            : m_sceneData->instanceTransform;
        for (const Parsers::MeshPart& part : m_sceneData->meshParts) {
            for (const Onyx::Domain::GpuVertex& v : part.vertices) {
                const glm::vec3 tp = glm::vec3(instanceTransform * glm::vec4(v.position, 1.0f));
                lo = glm::min(lo, tp);
                hi = glm::max(hi, tp);
                any = true;
            }
        }
    }
    if (!any) {
        m_bounds = Onyx::Domain::BoundingBox{};
        return;
    }
    m_bounds.min = lo;
    m_bounds.max = hi;
}

void Viewport3D::EnsureVulkanReady() {
    if (m_vkReady) return;

    m_ctx = Onyx::Rendering::GetGlobalContext();
    if (!m_ctx) return; // Vulkan not up yet (or Window is already tearing down) -- retry next Draw()

    std::string err;
    bool ok = Onyx::Rendering::Pipelines::CreateScene(*m_ctx, m_vk->scenePipelines, err) &&
              Onyx::Rendering::Pipelines::CreateGrid(*m_ctx, m_vk->gridPipeline, err) &&
              Onyx::Rendering::Pipelines::CreateBackground(*m_ctx, m_vk->backgroundPipeline, err) &&
              Onyx::Rendering::Pipelines::CreateOverlay(*m_ctx, m_vk->overlayPipeline, err);
    if (!ok) {
        ONYX_LOGF_ERR("[Viewport3D] Vulkan pipeline creation failed: %s", err.c_str());
        Onyx::Rendering::Pipelines::Destroy(*m_ctx, m_vk->overlayPipeline);
        Onyx::Rendering::Pipelines::Destroy(*m_ctx, m_vk->backgroundPipeline);
        Onyx::Rendering::Pipelines::Destroy(*m_ctx, m_vk->gridPipeline);
        Onyx::Rendering::Pipelines::Destroy(*m_ctx, m_vk->scenePipelines);
        m_ctx = nullptr;
        return;
    }

    m_texPool = std::make_unique<Onyx::App::TexturePool>(*m_ctx);
    m_vkReady = true;
}

void Viewport3D::ResizeTarget(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == m_fboWidth && height == m_fboHeight) return;
    if (!m_ctx) return;

    if (m_vk->targetCreated) {
        m_vk->target.Destroy(*m_ctx);
        m_vk->targetCreated = false;
    }

    std::string err;
    if (!m_vk->target.Create(*m_ctx, width, height, err)) {
        ONYX_LOGF_ERR("[Viewport3D] OffscreenTarget::Create failed: %s", err.c_str());
        m_fboWidth = m_fboHeight = 0;
        return;
    }
    m_vk->targetCreated = true;
    m_fboWidth = width;
    m_fboHeight = height;
    m_needsRedraw = true;

    // "AddTexture once per resize, not per frame" (the brief's own
    // wording): re-register the descriptor against the NEW resolve view,
    // deferred-retiring whatever m_displayTexId pointed at before.
    ImTextureID newId = m_texPool->RegisterExternalView(
        m_vk->target.ResolveView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_displayTexId, err);
    if (newId == ImTextureID_Invalid) {
        // T10 fix-round-1 (LOW): RegisterExternalView already kept the OLD
        // descriptor alive on failure (never retired it) -- keep displaying
        // it too, rather than clobbering m_displayTexId with the failure.
        ONYX_LOGF_ERR("[Viewport3D] TexturePool::RegisterExternalView failed: %s", err.c_str());
        return;
    }
    m_displayTexId = newId;
}

void Viewport3D::RenderFrame(int width, int height) {
    if (!m_ctx || !m_vk->targetCreated) return;

    auto* cfg = Onyx::Services::AppConfig::Get();
    const bool hasContent = HasBatches();

    glm::vec3 top, bottom;
    if (hasContent && cfg) {
        top = glm::vec3(cfg->bgTopR, cfg->bgTopG, cfg->bgTopB);
        bottom = glm::vec3(cfg->bgBotR, cfg->bgBotG, cfg->bgBotB);
    } else if (hasContent) {
        top = bgTopColor; bottom = bgBottomColor;
    } else if (cfg) {
        top = glm::vec3(cfg->bgTopR, cfg->bgTopG, cfg->bgTopB) * 0.8f;
        bottom = glm::vec3(cfg->bgBotR, cfg->bgBotG, cfg->bgBotB) * 0.8f;
    } else {
        top = glm::vec3(0.14f, 0.14f, 0.16f);
        bottom = glm::vec3(0.06f, 0.06f, 0.08f);
    }

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const glm::mat4 view = m_camera.GetViewMatrix();
    const glm::mat4 proj = VulkanProjection(m_camera.GetProjectionMatrix(aspect));

    const glm::vec4 gridColor = cfg ? glm::vec4(cfg->gridR, cfg->gridG, cfg->gridB, cfg->gridA)
                                     : glm::vec4(0.35f, 0.35f, 0.35f, 0.5f);

    // Same cfg-or-fallback pattern as gridColor above: RenderSkeleton
    // (Onyx_Render) has no AppConfig dependency, so the resolution happens
    // here, in the Shell-linked caller. Fallback matches the constant
    // RenderSkeleton always hardcoded before Task 5.
    const glm::vec4 boneColor = cfg ? glm::vec4(cfg->boneR, cfg->boneG, cfg->boneB, 1.0f)
                                     : glm::vec4(0.0f, 1.0f, 0.4f, 1.0f);

    std::string err;
    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool ok = Resources::OneShot(*m_ctx, [&](VkCommandBuffer cmd) {
        m_vk->target.BeginFrame(cmd, clear);

        std::string bgErr;
        if (!m_vk->sceneRendererVk.RenderBackground(*m_ctx, m_vk->backgroundPipeline, cmd, top, bottom, bgErr))
            ONYX_LOGF_ERR("[Viewport3D] RenderBackground failed: %s", bgErr.c_str());

        if (hasContent)
            m_vk->sceneRendererVk.Render(cmd, view, proj, shadingMode, width, height);

        if (showBones && m_sceneData && m_sceneData->HasSkeleton()) {
            std::string skelErr;
            if (!m_vk->sceneRendererVk.RenderSkeleton(*m_ctx, m_vk->overlayPipeline, cmd, view, proj, boneColor,
                                                       width, height, skelErr))
                ONYX_LOGF_ERR("[Viewport3D] RenderSkeleton failed: %s", skelErr.c_str());
        }

        if (showGrid) {
            std::string gridErr;
            if (!m_vk->sceneRendererVk.RenderGrid(*m_ctx, m_vk->gridPipeline, cmd, view, proj, gridColor, 1.0f,
                                                  width, height, gridErr))
                ONYX_LOGF_ERR("[Viewport3D] RenderGrid failed: %s", gridErr.c_str());
        }

        m_vk->target.EndFrame(cmd);
        m_vk->target.PrepareForSampling(cmd);
    }, err);

    if (!ok) {
        ONYX_LOGF_ERR("[Viewport3D] RenderFrame OneShot failed: %s", err.c_str());
    }
}

void Viewport3D::Draw() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x <= 0 || avail.y <= 0) return;

    EnsureVulkanReady();

    // Reserve a strip at the bottom of the viewport for the animation
    // transport/clip-selector. Image render area shrinks by that height so
    // the strip lives directly under the 3D scene. Recovered from the
    // pre-Vulkan Viewport3D (T6 restoration) -- receiver swapped from the
    // deleted GL m_sceneRenderer to m_vk->sceneRendererVk; GetAnimPlayer()
    // is a safe plain-pointer read even before EnsureVulkanReady() has
    // succeeded (nullptr until SetAnimation() is ever called).
    //
    // Fix round 1 (task-6-report.md): the original condition ("clip already
    // loaded") left no reachable path to ever LOAD one -- the clip browser
    // that called SetAnimation() lived in DrawInspector(), which has no
    // caller anywhere in this Shell (verified tree-wide). Keyed on
    // HasAnimations() instead: a scene with clips but nothing chosen yet
    // gets the compact DrawClipSelector() strip; once a clip is loaded, the
    // full DrawTransportBar() takes over. A scene with no animations at all
    // gets neither, exactly as before.
    Onyx::Rendering::AnimationPlayer* transportPlayer = m_vk->sceneRendererVk.GetAnimPlayer();
    const bool hasClip = transportPlayer && transportPlayer->GetCurrentActIndex() >= 0 &&
        transportPlayer->GetFrameCount() > 0;
    const bool hasAnimations = m_vk->sceneRendererVk.HasAnimations();
    const float transportHeight = hasClip ? kAnimTransportHeight
                                 : (hasAnimations ? kAnimSelectorHeight : 0.0f);

    ImVec2 viewSize(avail.x, std::max(50.0f, avail.y - transportHeight));

    // Publish whichever player this viewport currently owns so cross-cutting
    // panels (Anim Curves, Dopesheet) can read its playhead. Recovered from
    // the pre-Vulkan Viewport3D (T6 restoration; originally in
    // DrawInspector(), moved here in fix round 1 for the same unreachable-
    // hook reason as the clip browser above -- DrawInspector() never runs,
    // so a broadcast placed there would never run either, and this is the
    // one call this whole task exists to make land). GetAnimPlayer() being
    // nullptr (no clip loaded) is exactly the right thing to publish: it's
    // what "nothing playing" should broadcast.
    Onyx::App::SetActiveAnimationPlayer(transportPlayer);

    // ── Animation update (every frame, regardless of redraw) ───────────
    // Mesh animation now has a Vulkan API (SceneRendererVk::UpdateAnimation,
    // this milestone's Task 3/5) -- restored beside the camera-flight
    // update it used to run alone next to (see this file's top comment).
    float currentTime = (float)ImGui::GetTime();
    float dt = (m_lastFrameTime > 0.0f) ? (currentTime - m_lastFrameTime) : 0.0f;
    m_lastFrameTime = currentTime;
    if (m_vkReady && m_vk->sceneRendererVk.UpdateAnimation(dt)) m_needsRedraw = true;
    if (m_camera.UpdateAnimation(dt)) {
        m_needsRedraw = true;
    }

    if (m_vkReady) {
        ResizeTarget((int)viewSize.x, (int)viewSize.y);

        if (m_needsRedraw && m_fboWidth > 0 && m_fboHeight > 0) {
            m_needsRedraw = false;
            RenderFrame(m_fboWidth, m_fboHeight);
        }
    }

    // ── Display ─────────────────────────────────────────────────────
    if (m_vkReady && m_displayTexId != ImTextureID_Invalid) {
        ImGui::Image(m_displayTexId, viewSize, ImVec2(0, 0), ImVec2(1, 1));
        m_viewportHovered = ImGui::IsItemHovered();
    } else {
        ImGui::InvisibleButton("##vp3d_pending", viewSize);
        m_viewportHovered = false;
    }
    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const ImVec2 imageMax = ImGui::GetItemRectMax();

    // ── Axis gizmo overlay ──────────────────────────────────────────
    Rendering::CameraView snapTarget;
    if (m_axisGizmo.Draw(m_camera.GetViewRotation(), imageMin, imageMax, snapTarget)) {
        m_camera.SnapToView(snapTarget);
        m_needsRedraw = true;
    }

    // ── Input ────────────────────────────────────────────────────────
    HandleInput();

    // ── Toolbar overlay ─────────────────────────────────────────────
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    DrawToolbar(avail, cursorPos);

    // ── Empty viewport message ────────────────────────────────────────
    if (!m_sceneData || m_sceneData->IsEmpty()) {
        const char* msg = "No mesh loaded";
        ImVec2 textSize = ImGui::CalcTextSize(msg);
        ImGui::SetCursorScreenPos(ImVec2(
            cursorPos.x + (avail.x - textSize.x) * 0.5f,
            cursorPos.y - avail.y * 0.5f - textSize.y * 0.5f
        ));
        ImGui::TextDisabled("%s", msg);
    }

    // ── Animation strip: compact clip selector, or the full transport once
    // a clip is chosen ──────────────────────────────────────────────────
    // Recovered from the pre-Vulkan Viewport3D (T6 restoration); split into
    // two widgets in fix round 1 so a clip can actually be reached (see the
    // height computation above).
    if (hasClip) {
        DrawTransportBar();
    } else if (hasAnimations) {
        DrawClipSelector();
    }

    if (m_texPool) m_texPool->AdvanceFrame();
}

void Viewport3D::HandleInput() {
    if (!m_viewportHovered) return;

    ImGuiIO& io = ImGui::GetIO();

    const bool gizmoHot = m_axisGizmo.IsHovered();

    if (!gizmoHot &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Right) &&
        (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
        m_camera.ProcessMouseDrag(io.MouseDelta.x, io.MouseDelta.y);
        m_needsRedraw = true;
    }
    if (!gizmoHot &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Middle) &&
        (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
        m_camera.ProcessMousePan(io.MouseDelta.x, io.MouseDelta.y);
        m_needsRedraw = true;
    }
    if (!gizmoHot && io.MouseWheel != 0.0f) {
        m_camera.ProcessScroll(io.MouseWheel);
        m_needsRedraw = true;
    }

    if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_F) && m_sceneData && !m_sceneData->IsEmpty()) {
            m_camera.FocusOn(m_bounds);
            m_needsRedraw = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
            // T11-review F3: Matcap/Wireframe/TexturedWire all render
            // identically to Solid or Textured on SceneRendererVk (no
            // Vulkan matcap/wireframe pass exists -- see that class's own
            // divergence-2 comment) -- cycling through five labels that
            // produce two distinct images was misleading. Toggle between
            // the two modes that actually look different; keep in sync
            // with DrawToolbar's identical cycle below and DrawInspector's
            // combo.
            shadingMode = (shadingMode == Rendering::ShadingMode::Solid)
                ? Rendering::ShadingMode::Textured
                : Rendering::ShadingMode::Solid;
            m_needsRedraw = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_G)) {
            showGrid = !showGrid;
            m_needsRedraw = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad5)) {
            m_camera.Reset();
            m_needsRedraw = true;
        }
    }
}

void Viewport3D::DrawToolbar(ImVec2 avail, ImVec2 cursorPos) {
    ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + 4, cursorPos.y - avail.y + 4));

    ImGui::PushStyleColor(ImGuiCol_Button, Onyx::Theme::ToolbarButton());
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Onyx::Theme::ToolbarButtonHover());
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Onyx::Theme::ToolbarButtonActive());

    namespace W = Onyx::App::Widgets;

    // T11-review F3: only Solid/Textured are offered any more -- see the
    // [Z]-shortcut handler's comment above for why.
    const char* shadingLabel =
        (shadingMode == Rendering::ShadingMode::Textured) ? "Textured" : "Solid";
    char shadingTip[64];
    snprintf(shadingTip, sizeof(shadingTip), "Shading: %s [Z]", shadingLabel);
    {
        W::IconButtonOpts opts;
        opts.tooltip = shadingTip;
        if (W::IconButton("vp_shading", ICON_SF_CUBE, opts)) {
            shadingMode = (shadingMode == Rendering::ShadingMode::Solid)
                ? Rendering::ShadingMode::Textured
                : Rendering::ShadingMode::Solid;
            m_needsRedraw = true;
        }
    }

    ImGui::SameLine();
    {
        W::IconButtonOpts opts;
        opts.tooltip  = "Toggle grid [G]";
        opts.selected = showGrid;
        if (W::IconButton("vp_grid", ICON_SF_SQUARE_GRID_3X3, opts)) {
            showGrid = !showGrid;
            m_needsRedraw = true;
        }
    }

    if (m_sceneData && !m_sceneData->IsEmpty()) {
        ImGui::SameLine();
        W::IconButtonOpts opts;
        opts.tooltip  = "Toggle object list";
        opts.selected = showObjectList;
        if (W::IconButton("vp_objlist", ICON_SF_LIST_BULLET, opts)) {
            showObjectList = !showObjectList;
        }
    }

    ImGui::SameLine();
    {
        W::IconButtonOpts opts;
        opts.tooltip = "Frame all [F]";
        if (W::IconButton("vp_focus", ICON_SF_VIEWFINDER, opts)) {
            if (m_sceneData && !m_sceneData->IsEmpty()) {
                m_camera.FocusOn(m_bounds);
            } else {
                m_camera.Reset();
            }
            m_needsRedraw = true;
        }
    }

    ImGui::SameLine();
    {
        Onyx::App::CameraPanel* panel = Onyx::App::CameraPanel::Get();
        const bool camOpen = panel ? panel->visible : false;
        W::IconButtonOpts opts;
        opts.tooltip  = "Camera settings";
        opts.selected = camOpen;
        if (W::IconButton("vp_cam", ICON_SF_CAMERA, opts)) {
            Onyx::App::CameraPanel::Toggle();
            if (panel && panel->visible) ImGui::SetWindowFocus("Camera");
        }
    }

    if (m_sceneData && m_sceneData->HasSkeleton()) {
        ImGui::SameLine();
        {
            W::IconButtonOpts opts;
            opts.tooltip  = "Toggle bones";
            opts.selected = showBones;
            if (W::IconButton("vp_bones", ICON_SF_FIGURE_WALK, opts)) {
                showBones = !showBones;
                m_needsRedraw = true;
            }
        }
    }

    // Stats
    if (HasBatches()) {
        int totalVerts = 0, totalTris = 0;
        for (const auto& b : m_vk->sceneRendererVk.GetBatches()) {
            totalVerts += b.vertexCount;
            totalTris  += b.triangleCount;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("| %d verts, %d tris", totalVerts, totalTris);
    }

    // FPS counter — bottom center
    {
        char fpsBuf[32];
        snprintf(fpsBuf, sizeof(fpsBuf), "%.0f FPS", ImGui::GetIO().Framerate);
        ImVec2 fpsSize = ImGui::CalcTextSize(fpsBuf);
        ImGui::SetCursorScreenPos(ImVec2(
            cursorPos.x + (avail.x - fpsSize.x) * 0.5f,
            cursorPos.y - fpsSize.y - 4.0f
        ));
        ImGui::TextDisabled("%s", fpsBuf);
    }

    ImGui::PopStyleColor(3);
}

// Fix round 1 (task-6-report.md): the clip browser this logic is ported
// from used to live in DrawInspector() (git show 71fe575^:Source/Viewers/
// Viewport3D.cpp, lines ~501-539) as a group/act TreeNodeEx tree with
// double-click-to-play. DrawInspector() has no caller anywhere in this
// Shell -- verified tree-wide, both at HEAD and at that historical commit
// -- so nothing living there is reachable, no matter how correct it is.
// Relocated into the strip Draw() reserves under the viewport (same strip
// DrawTransportBar() below draws into, mutually exclusive with it), and
// simplified from a multi-row tree to a single combo to match that strip's
// compact horizontal idiom rather than importing the inspector's tree
// styling wholesale. The underlying logic -- skip isExternal groups
// (AnimationPlayer::SetAnimation refuses them anyway, Source/Rendering/
// AnimationPlayer.cpp's own `if (group.isExternal) return;`), skip groups
// with no acts, restrict to the skinning data type via
// FindSkinningTypeIndex(), call SetAnimation(ig, ia) on selection -- is
// unchanged from the historical browser.
void Viewport3D::DrawClipSelector() {
    const Parsers::AnimationData* animData = m_vk->sceneRendererVk.GetAnimationData();
    if (!animData) return; // HasAnimations() was already checked by the caller; defensive only

    ImVec4 bgCol = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(bgCol.x, bgCol.y, bgCol.z, 0.9f));
    ImGui::BeginChild("##clip_selector", ImVec2(0, 0), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    struct ClipEntry { int groupIdx; int actIdx; std::string label; };
    std::vector<ClipEntry> entries;
    const int skinIdx = animData->FindSkinningTypeIndex();
    if (skinIdx >= 0) {
        for (int ig = 0; ig < (int)animData->groups.size(); ++ig) {
            const auto& group = animData->groups[ig];
            // isExternal groups are omitted outright rather than listed
            // disabled -- SetAnimation() silently no-ops on them, and this
            // Shell has no per-file act catalog to resolve an external
            // reference against, so a disabled entry could only ever say
            // "not available here" with no path to make it available.
            if (group.isExternal || group.acts.empty()) continue;
            for (int ia = 0; ia < (int)group.acts.size(); ++ia) {
                const auto& act = group.acts[ia];
                char label[160];
                snprintf(label, sizeof(label), "%s: %s  [%.1fs]",
                         group.name.c_str(), act.name.c_str(), act.duration);
                entries.push_back({ig, ia, std::string(label)});
            }
        }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Clip:");
    ImGui::SameLine();

    if (entries.empty()) {
        ImGui::TextDisabled("No playable clips (only external groups present).");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    ImGui::SetNextItemWidth(std::min(360.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo("##clip_combo", "Select a clip...")) {
        for (const ClipEntry& entry : entries) {
            if (ImGui::Selectable(entry.label.c_str(), false)) {
                // AnimationPlayer::SetAnimation leaves the new act playing
                // (m_playing = true at the end of SetAnimation()), so the
                // very next Draw() sees hasClip == true and switches this
                // strip over to DrawTransportBar() automatically.
                m_vk->sceneRendererVk.SetAnimation(entry.groupIdx, entry.actIdx);
                m_needsRedraw = true;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// Recovered from the pre-Vulkan Viewport3D (T6 restoration) -- receiver
// swapped from the deleted GL m_sceneRenderer to m_vk->sceneRendererVk;
// every AnimationPlayer/DrawAnimationTimeline call below is otherwise
// unchanged from git show 71fe575^:Source/Viewers/Viewport3D.cpp.
void Viewport3D::DrawTransportBar() {
    Onyx::Rendering::AnimationPlayer* player = m_vk->sceneRendererVk.GetAnimPlayer();
    if (!player || player->GetCurrentActIndex() < 0) return;

    bool  isPlaying   = player->IsPlaying();
    float dur         = player->GetDuration();
    int   totalFrames = std::max(1, player->GetFrameCount());
    int   curFrame    = player->GetCurrentFrame();

    // Tinted background that visually separates the strip from the 3D image.
    ImVec4 bgCol = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(bgCol.x, bgCol.y, bgCol.z, 0.9f));
    ImGui::BeginChild("##transport", ImVec2(0, 0), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Row 1: transport buttons + speed + loop mode
    const float btnW = 28.0f;
    if (ImGui::Button("|<", ImVec2(btnW, 0))) { player->SetFrame(0); m_needsRedraw = true; }
    ImGui::SameLine();
    if (ImGui::Button("<",  ImVec2(btnW, 0))) { player->SetFrame(curFrame - 1); m_needsRedraw = true; }
    ImGui::SameLine();
    if (ImGui::Button(isPlaying ? "Pause" : "Play", ImVec2(60, 0))) {
        if (dur > 0.0f) { player->Toggle(); m_needsRedraw = true; }
    }
    ImGui::SameLine();
    if (ImGui::Button(">",  ImVec2(btnW, 0))) { player->SetFrame(curFrame + 1); m_needsRedraw = true; }
    ImGui::SameLine();
    if (ImGui::Button(">|", ImVec2(btnW, 0))) { player->SetFrame(totalFrames - 1); m_needsRedraw = true; }
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(50, 0))) { m_vk->sceneRendererVk.StopAnimation(); m_needsRedraw = true; }

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
    ImGui::Text("F %d/%d  %.2fs/%.2fs", curFrame, totalFrames - 1, player->GetTime(), dur);

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
    ImGui::PushItemWidth(70);
    float speed = player->GetSpeed();
    if (ImGui::DragFloat("##speed", &speed, 0.05f, -4.0f, 4.0f, "%.2fx")) {
        player->SetSpeed(speed);
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();

    const struct { const char* lbl; float val; } presets[] = {
        {".25", 0.25f}, {".5", 0.5f}, {"1x", 1.0f}, {"2x", 2.0f}, {"-1x", -1.0f},
    };
    for (auto& p : presets) {
        if (ImGui::SmallButton(p.lbl)) player->SetSpeed(p.val);
        ImGui::SameLine();
    }

    ImGui::PushItemWidth(90);
    int loopMode = (int)player->GetLoopMode();
    const char* loopLabels[] = { "No Loop", "Loop", "PingPong" };
    if (ImGui::Combo("##loop", &loopMode, loopLabels, IM_ARRAYSIZE(loopLabels))) {
        player->SetLoopMode((Rendering::AnimationPlayer::LoopMode)loopMode);
    }
    ImGui::PopItemWidth();

    // Keyboard shortcuts: Space/arrows/Home/End. Active while the viewport
    // window has focus (covers both the image hover and the transport).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
        m_viewportHovered) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            if (dur > 0.0f) { player->Toggle(); m_needsRedraw = true; }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
            player->SetFrame(player->GetCurrentFrame() - 1); m_needsRedraw = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
            player->SetFrame(player->GetCurrentFrame() + 1); m_needsRedraw = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
            player->SetFrame(0); m_needsRedraw = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            player->SetFrame(totalFrames - 1); m_needsRedraw = true;
        }
    }

    // Row 2: rich timeline with frame ticks, keyframe markers, scrub
    if (Onyx::App::DrawAnimationTimeline("anim_timeline", *player)) {
        m_needsRedraw = true;
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void Viewport3D::DrawInspector() {
    ImGui::Text("Viewport Settings");
    ImGui::Separator();

    // T11-review F3: Matcap/Wireframe/TexturedWire removed from this list
    // (rather than kept and disabled) -- SceneRendererVk aliases all three
    // to Solid or Textured (no Vulkan matcap/wireframe pass exists), so
    // they never produced a distinct image; offering five labels for two
    // results was misleading. shadingMode's enum still has all five values
    // (nothing else in this class sets it to the removed three), so the
    // combo index maps explicitly rather than casting -- keep this in sync
    // with the [Z]-shortcut/toolbar-button cycle in HandleInput/DrawToolbar.
    const char* shadingLabel = "Solid\0Textured\0";
    int mode = (shadingMode == Rendering::ShadingMode::Textured) ? 1 : 0;
    if (ImGui::Combo("Shading", &mode, shadingLabel)) {
        shadingMode = (mode == 1) ? Rendering::ShadingMode::Textured : Rendering::ShadingMode::Solid;
        m_needsRedraw = true;
    }

    if (ImGui::Checkbox("Show Grid", &showGrid)) m_needsRedraw = true;
    if (m_sceneData && m_sceneData->HasSkeleton()) {
        if (ImGui::Checkbox("Show Bones", &showBones)) m_needsRedraw = true;
    }

    // NOTE (T6 fix round 1, task-6-report.md): the animation clip browser
    // and the Onyx::App::SetActiveAnimationPlayer() broadcast used to live
    // here. Both were moved to the strip Draw() reserves under the 3D
    // image (DrawClipSelector() / DrawTransportBar()) because
    // IDocumentContent::DrawInspector() -- this very method -- has NO
    // caller anywhere in this Shell. Verified tree-wide, both at HEAD and
    // at the historical commit this class's animation UI was recovered
    // from (71fe575^): DocumentWindow::Draw() calls tab->Draw() on the
    // active tab every frame, never tab->DrawInspector(); the panel
    // actually titled "Inspector" (InspectorPanel -> InfoTab,
    // Source/App/Panels/InspectorPanel.cpp) is an unrelated asset-metadata
    // viewer wired to Workspace's selection bus, not to this hook; and
    // CameraPanel resolves the active viewport via GetEmbeddedViewport()
    // but only ever touches GetCamera(). This is pre-existing Shell debt
    // (likely a residue of the InfoTab migration), not introduced by this
    // task, and wiring a caller is out of this task's scope (it would mean
    // editing InspectorPanel.cpp, not Viewport3D). Left recorded here, in
    // plain text, so the next person doesn't put something load-bearing
    // in this method and lose it the same way.

    // ── Mesh Batches ────────────────────────────────────────────────
    ImGui::Separator();
    ImGui::Text("Scene Mesh Batches");

    auto& batches = m_vk->sceneRendererVk.GetBatches();
    if (batches.empty()) {
        ImGui::TextDisabled("No meshes in scene.");
        return;
    }

    // Group consecutive batches that share the same non-zero meshHash --
    // same LOD-grouping heuristic the GL inspector used (each shared LOD
    // blob is referenced by N consecutive submeshes).
    struct LodGroup { uint64_t hash; std::vector<size_t> idx; };
    std::vector<LodGroup> groups;
    for (size_t i = 0; i < batches.size(); ++i) {
        const uint64_t h = batches[i].meshHash;
        if (h != 0 && !groups.empty() && groups.back().hash == h) {
            groups.back().idx.push_back(i);
        } else {
            groups.push_back({h, {i}});
        }
    }

    auto renderVisibilityToggle = [this](Rendering::RenderBatch& batch) {
        bool prev = batch.isVisible;
        ImGui::Checkbox("##vis", &batch.isVisible);
        bool itemHovered = ImGui::IsItemHovered();
        bool toggledByClick = (prev != batch.isVisible);

        if (toggledByClick) {
            m_dragToggleActive = true;
            m_dragToggleValue  = batch.isVisible;
            m_needsRedraw = true;
        } else if (m_dragToggleActive && itemHovered &&
                   ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (batch.isVisible != m_dragToggleValue) {
                batch.isVisible = m_dragToggleValue;
                m_needsRedraw = true;
            }
        }
    };

    auto renderHighlightOnHover = [this](Rendering::RenderBatch& batch) {
        bool hovered = ImGui::IsItemHovered();
        if (hovered != batch.isHighlighted) {
            batch.isHighlighted = hovered;
            m_needsRedraw = true;
        }
    };

    ImGui::BeginChild("MeshBatches", ImVec2(0, 0), true);

    for (size_t g = 0; g < groups.size(); ++g) {
        const auto& grp = groups[g];

        if (grp.idx.size() == 1) {
            size_t i = grp.idx[0];
            auto& batch = batches[i];
            ImGui::PushID((int)i);
            renderVisibilityToggle(batch);
            ImGui::SameLine();
            std::string label = batch.name.empty() ? ("Part " + std::to_string(i)) : batch.name;
            if (batch.meshHash == 0) label += "  (internal)";
            ImGui::Selectable(label.c_str(), false);
            renderHighlightOnHover(batch);
            ImGui::PopID();
            continue;
        }

        ImGui::PushID((int)(1000 + g));

        bool anyVisible = false, allVisible = true;
        for (size_t i : grp.idx) {
            if (batches[i].isVisible) anyVisible = true;
            else                       allVisible = false;
        }
        bool groupVis = anyVisible;
        bool prevGroupVis = groupVis;
        ImGui::Checkbox("##groupvis", &groupVis);
        bool groupHovered = ImGui::IsItemHovered();
        bool groupClicked = (prevGroupVis != groupVis);

        if (groupClicked) {
            for (size_t i : grp.idx) batches[i].isVisible = groupVis;
            m_dragToggleActive = true;
            m_dragToggleValue  = groupVis;
            m_needsRedraw = true;
        } else if (m_dragToggleActive && groupHovered &&
                   ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            bool wantVis = m_dragToggleValue;
            if (anyVisible != wantVis || !allVisible) {
                for (size_t i : grp.idx) batches[i].isVisible = wantVis;
                m_needsRedraw = true;
            }
        }

        ImGui::SameLine();

        char header[96];
        std::snprintf(header, sizeof(header),
                      "Mesh Group %zu  (%zu LODs, hash %016llX)",
                      g, grp.idx.size(),
                      (unsigned long long)grp.hash);

        bool open = ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_SpanAvailWidth);

        if (open) {
            for (size_t k = 0; k < grp.idx.size(); ++k) {
                size_t i = grp.idx[k];
                auto& batch = batches[i];
                ImGui::PushID((int)i);
                renderVisibilityToggle(batch);
                ImGui::SameLine();
                char lodLabel[96];
                std::snprintf(lodLabel, sizeof(lodLabel),
                              "LOD %zu  (%dv, %dt)",
                              k, batch.vertexCount, batch.triangleCount);
                ImGui::Selectable(lodLabel, false);
                renderHighlightOnHover(batch);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    ImGui::EndChild();

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_dragToggleActive = false;
    }
}

} // namespace Onyx::Viewers
