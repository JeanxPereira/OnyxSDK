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
// them directly and broke the build: <wingdi.h>'s `#define TextOut
// TextOutW` (or `TextOutA`) collided with `Onyx::Modules::TextOut`, a real
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
// separate command buffer via Onyx::RenderVk::Resources::OneShot (the same
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
// Feature gaps inherited from SceneRendererVk (Include/Onyx/RenderVk/
// SceneRendererVk.h), not introduced by this task -- do not "fix" these
// here, per the brief's "MSAA + outline parity comes from the renderer --
// do not reimplement effects in the viewer":
//   - No animation playback API (SetAnimation/UpdateAnimation/AnimPlayer)
//     exists on SceneRendererVk this milestone -- every skinned scene
//     renders its rest pose. The GL path's transport bar/clip browser/
//     play-pause UI is therefore dropped entirely rather than wired
//     against nothing; showBones + RenderSkeleton() still work (rest
//     pose).
//   - Render() does not read RenderBatch::isVisible/isHighlighted (no
//     per-batch culling, no hover-outline pass) -- the inspector's
//     visibility checkboxes and hover highlight still mutate those fields
//     (via GetBatches(), the exact same Rendering::RenderBatch struct GL's
//     SceneRenderer also fills -- see SceneRendererVk.h's own "RenderBatch
//     reuse" comment), they simply have no visible effect yet.
//
// Y-flip on display: GL's ImGui::Image() call used uv0=(0,1)/uv1=(1,0) to
// flip vertically (GL's texture origin is bottom-left). Vulkan's resolve
// image is already top-down in memory (OffscreenTarget::Readback's own
// doc comment: "empirically verified top-down for this target") and
// ImGui's own UV convention is top-down too, so the Vulkan path below
// draws with the plain uv0=(0,0)/uv1=(1,1) -- no flip.
// ═══════════════════════════════════════════════════════════════════════

#include <Onyx/Viewers/Viewport3D.h>
#include <Onyx/App/TexturePool.h>
#include <Onyx/RenderVk/OffscreenTarget.h>
#include <Onyx/RenderVk/Pipelines.h>
#include <Onyx/RenderVk/SceneRendererVk.h>
#include <Onyx/RenderVk/VkContext.h>
#include <Onyx/Services/Events.h>
#include <imgui.h>
#include <Onyx/Services/AppConfig.h>
#include <Onyx/Services/Logger.h>
#include <Onyx/Services/ThemeManager.h>
#include <Onyx/Fonts/SFSymbols.h>
#include <Onyx/App/Panels/CameraPanel.h>
#include <Onyx/App/Widgets.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <limits>

namespace Onyx::Viewers {

using Onyx::RenderVk::Resources;
using Onyx::RenderVk::VulkanProjection;

// ── VulkanState -- see Viewport3D.h's top comment for why this is a
// forward-declared, .cpp-only struct ─────────────────────────────────────
struct Viewport3D::VulkanState {
    Onyx::RenderVk::ScenePipelines     scenePipelines;
    Onyx::RenderVk::GridPipeline       gridPipeline;
    Onyx::RenderVk::BackgroundPipeline backgroundPipeline;
    Onyx::RenderVk::OverlayPipeline    overlayPipeline;
    Onyx::RenderVk::SceneRendererVk    sceneRendererVk;
    Onyx::RenderVk::OffscreenTarget    target;
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
    m_texPool.reset(); // safe either way -- TexturePool::~TexturePool() carries the same guard itself

    Onyx::RenderVk::VkContext* live = Onyx::RenderVk::GetGlobalContext();
    if (live && m_vkReady) {
        m_vk->sceneRendererVk.Clear(*live);
        if (m_vk->targetCreated) m_vk->target.Destroy(*live);
        Onyx::RenderVk::Pipelines::Destroy(*live, m_vk->overlayPipeline);
        Onyx::RenderVk::Pipelines::Destroy(*live, m_vk->backgroundPipeline);
        Onyx::RenderVk::Pipelines::Destroy(*live, m_vk->gridPipeline);
        Onyx::RenderVk::Pipelines::Destroy(*live, m_vk->scenePipelines);
    }
}

std::string Viewport3D::GetName() const { return m_name; }

bool Viewport3D::HasBatches() const {
    return !m_vk->sceneRendererVk.GetBatches().empty();
}

void Viewport3D::ClearScene() {
    m_sceneData.reset();
    m_bounds = Onyx::Domain::BoundingBox{};
    if (m_vkReady) {
        Onyx::RenderVk::VkContext* live = Onyx::RenderVk::GetGlobalContext();
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
    LOG_WARN("[Viewport3D] LoadFromMeshData: not ported to Vulkan (dead code path, no callers) -- ignored");
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
            LOG_ERR("[Viewport3D] SceneRendererVk::Build failed: %s", err.c_str());
        }
    }
}

void Viewport3D::ComputeBounds() {
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(-std::numeric_limits<float>::max());
    bool any = false;
    if (m_sceneData) {
        for (const Parsers::MeshPart& part : m_sceneData->meshParts) {
            for (const Onyx::Domain::GpuVertex& v : part.vertices) {
                lo = glm::min(lo, v.position);
                hi = glm::max(hi, v.position);
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

    m_ctx = Onyx::RenderVk::GetGlobalContext();
    if (!m_ctx) return; // Vulkan not up yet (or Window is already tearing down) -- retry next Draw()

    std::string err;
    bool ok = Onyx::RenderVk::Pipelines::CreateScene(*m_ctx, m_vk->scenePipelines, err) &&
              Onyx::RenderVk::Pipelines::CreateGrid(*m_ctx, m_vk->gridPipeline, err) &&
              Onyx::RenderVk::Pipelines::CreateBackground(*m_ctx, m_vk->backgroundPipeline, err) &&
              Onyx::RenderVk::Pipelines::CreateOverlay(*m_ctx, m_vk->overlayPipeline, err);
    if (!ok) {
        LOG_ERR("[Viewport3D] Vulkan pipeline creation failed: %s", err.c_str());
        Onyx::RenderVk::Pipelines::Destroy(*m_ctx, m_vk->overlayPipeline);
        Onyx::RenderVk::Pipelines::Destroy(*m_ctx, m_vk->backgroundPipeline);
        Onyx::RenderVk::Pipelines::Destroy(*m_ctx, m_vk->gridPipeline);
        Onyx::RenderVk::Pipelines::Destroy(*m_ctx, m_vk->scenePipelines);
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
        LOG_ERR("[Viewport3D] OffscreenTarget::Create failed: %s", err.c_str());
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
        LOG_ERR("[Viewport3D] TexturePool::RegisterExternalView failed: %s", err.c_str());
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

    std::string err;
    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool ok = Resources::OneShot(*m_ctx, [&](VkCommandBuffer cmd) {
        m_vk->target.BeginFrame(cmd, clear);

        std::string bgErr;
        if (!m_vk->sceneRendererVk.RenderBackground(*m_ctx, m_vk->backgroundPipeline, cmd, top, bottom, bgErr))
            LOG_ERR("[Viewport3D] RenderBackground failed: %s", bgErr.c_str());

        if (hasContent)
            m_vk->sceneRendererVk.Render(cmd, view, proj, shadingMode, width, height);

        if (showBones && m_sceneData && m_sceneData->HasSkeleton()) {
            std::string skelErr;
            if (!m_vk->sceneRendererVk.RenderSkeleton(*m_ctx, m_vk->overlayPipeline, cmd, view, proj, width, height, skelErr))
                LOG_ERR("[Viewport3D] RenderSkeleton failed: %s", skelErr.c_str());
        }

        if (showGrid) {
            std::string gridErr;
            if (!m_vk->sceneRendererVk.RenderGrid(*m_ctx, m_vk->gridPipeline, cmd, view, proj, gridColor, 1.0f,
                                                  width, height, gridErr))
                LOG_ERR("[Viewport3D] RenderGrid failed: %s", gridErr.c_str());
        }

        m_vk->target.EndFrame(cmd);
        m_vk->target.PrepareForSampling(cmd);
    }, err);

    if (!ok) {
        LOG_ERR("[Viewport3D] RenderFrame OneShot failed: %s", err.c_str());
    }
}

void Viewport3D::Draw() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x <= 0 || avail.y <= 0) return;

    EnsureVulkanReady();

    ImVec2 viewSize(avail.x, std::max(50.0f, avail.y));

    // ── Camera flight animation only (mesh animation has no Vulkan API
    // this milestone -- see this file's top comment) ───────────────────
    float currentTime = (float)ImGui::GetTime();
    float dt = (m_lastFrameTime > 0.0f) ? (currentTime - m_lastFrameTime) : 0.0f;
    m_lastFrameTime = currentTime;
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
            switch (shadingMode) {
                case Rendering::ShadingMode::Solid:        shadingMode = Rendering::ShadingMode::Matcap;       break;
                case Rendering::ShadingMode::Matcap:       shadingMode = Rendering::ShadingMode::Textured;     break;
                case Rendering::ShadingMode::Textured:     shadingMode = Rendering::ShadingMode::Wireframe;    break;
                case Rendering::ShadingMode::Wireframe:    shadingMode = Rendering::ShadingMode::TexturedWire; break;
                case Rendering::ShadingMode::TexturedWire: shadingMode = Rendering::ShadingMode::Solid;        break;
            }
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

    const char* shadingLabel = nullptr;
    switch (shadingMode) {
        case Rendering::ShadingMode::Solid:        shadingLabel = "Solid";      break;
        case Rendering::ShadingMode::Matcap:       shadingLabel = "Matcap";     break;
        case Rendering::ShadingMode::Textured:     shadingLabel = "Textured";   break;
        case Rendering::ShadingMode::Wireframe:    shadingLabel = "Wire";       break;
        case Rendering::ShadingMode::TexturedWire: shadingLabel = "Wire (Tex)"; break;
    }
    char shadingTip[64];
    snprintf(shadingTip, sizeof(shadingTip), "Shading: %s [Z]", shadingLabel);
    {
        W::IconButtonOpts opts;
        opts.tooltip = shadingTip;
        if (W::IconButton("vp_shading", ICON_SF_CUBE, opts)) {
            switch (shadingMode) {
                case Rendering::ShadingMode::Solid:        shadingMode = Rendering::ShadingMode::Matcap;       break;
                case Rendering::ShadingMode::Matcap:       shadingMode = Rendering::ShadingMode::Textured;     break;
                case Rendering::ShadingMode::Textured:     shadingMode = Rendering::ShadingMode::Wireframe;    break;
                case Rendering::ShadingMode::Wireframe:    shadingMode = Rendering::ShadingMode::TexturedWire; break;
                case Rendering::ShadingMode::TexturedWire: shadingMode = Rendering::ShadingMode::Solid;        break;
            }
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

void Viewport3D::DrawInspector() {
    ImGui::Text("Viewport Settings");
    ImGui::Separator();

    const char* shadingLabel = "Solid\0Matcap\0Textured\0Wireframe\0TexturedWire\0";
    int mode = (int)shadingMode;
    if (ImGui::Combo("Shading", &mode, shadingLabel)) {
        shadingMode = (Rendering::ShadingMode)mode;
        m_needsRedraw = true;
    }

    if (ImGui::Checkbox("Show Grid", &showGrid)) m_needsRedraw = true;
    if (m_sceneData && m_sceneData->HasSkeleton()) {
        if (ImGui::Checkbox("Show Bones", &showBones)) m_needsRedraw = true;
    }

    if (m_sceneData && m_sceneData->animations) {
        ImGui::Separator();
        ImGui::TextDisabled("Animation clips are present but playback has no Vulkan renderer "
                            "API yet (M4 gap) -- scenes render their rest pose.");
    }

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
