#include "Onyx/Ui/Panels/Viewport.h"

#include "AxisGizmo.h"

#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Gpu/Device.h>
#include <Onyx/Gpu/FrameRing.h>
#include <Onyx/Gpu/RenderTarget.h>
#include <Onyx/Gpu/Upload.h>
#include <Onyx/Render/SceneRenderer.h>
#include <Onyx/Ui/FrameClock.h>
#include <Onyx/Ui/TexturePool.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Viewers/SceneView.h>
#include <Onyx/Ui/Widgets.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace Onyx::Ui {

namespace {

constexpr float kGizmoMargin = 16.0f;
constexpr float kGizmoRadius = 38.0f;

uint32_t Pixels(float points, float scale) {
    return static_cast<uint32_t>(std::max(1.0f, std::floor(points * scale)));
}

void ShowFault(const Error& error) {
    Widgets::StatusText(Services::Severity::Error, (error.code + ": " + error.message).c_str());
}

}

Viewport::Viewport() = default;

Viewport::~Viewport() {
    if (m_pool && m_texture != ImTextureID_Invalid) m_pool->Remove(m_texture);
}

uint64_t Viewport::TargetGeneration() const { return m_target ? m_target->Generation() : 0; }

bool Viewport::EnsureDevice(UiContext& ui) {
    if (m_renderer) return true;
    auto renderer = Render::SceneRenderer::Create(*ui.gpu);
    if (!renderer.ok()) {
        m_fault = renderer.error();
        return false;
    }
    auto upload = Gpu::UploadContext::Create(*ui.gpu);
    if (!upload.ok()) {
        m_fault = upload.error();
        return false;
    }
    m_renderer = std::move(renderer.value());
    m_upload = std::move(upload.value());
    return true;
}

bool Viewport::EnsureTarget(UiContext& ui, uint32_t width, uint32_t height) {
    const VkExtent2D extent{width, height};
    if (!m_target) {
        Gpu::RenderTargetDesc desc;
        desc.extent = extent;
        desc.colorFormat = m_renderer->Options().colorFormat;
        desc.depthFormat = m_renderer->Options().depthFormat;
        desc.samples = m_renderer->Options().samples;
        auto target = Gpu::RenderTarget::Create(*ui.gpu, desc);
        if (!target.ok()) {
            m_fault = target.error();
            return false;
        }
        m_target = std::move(target.value());
    } else if (m_target->Extent().width != width || m_target->Extent().height != height) {
        // Nothing recorded in this frame touches the target yet (Draw records after EnsureTarget), so
        // the old images and the old descriptor last served an earlier, submitted frame; both retire
        // at LastSubmitted.
        auto resized = m_target->Resize(extent);
        if (!resized.ok()) {
            m_fault = resized.error();
            return false;
        }
    }

    if (m_texture == ImTextureID_Invalid || m_registered != m_target->Generation()) {
        auto texture = ui.textures->Register(m_target->ResolveView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!texture.ok()) {
            m_fault = texture.error();
            return false;
        }
        if (m_pool && m_texture != ImTextureID_Invalid) m_pool->Remove(m_texture);
        m_pool = ui.textures;
        m_texture = texture.value();
        m_registered = m_target->Generation();
    }
    return true;
}

void Viewport::Draw(UiContext& ui) {
    if (!ui.gpu || !ui.frame || !ui.textures) {
        Widgets::Hint("The viewport needs a GPU device.");
        return;
    }
    SceneView* view = ActiveSceneView(ui);
    if (!view) {
        Widgets::Hint("Open a mesh to see it here.");
        return;
    }
    if (!m_renderer && m_fault && m_failedDevice == ui.gpu) {
        ShowFault(*m_fault);
        return;
    }
    if (!EnsureDevice(ui)) {
        m_failedDevice = ui.gpu;
        ShowFault(*m_fault);
        return;
    }
    m_failedDevice = nullptr;

    SceneSession& session = view->Session();
    Modules::Document* doc = ui.workspace.Get(view->Document());
    Services::DiagSink* diags = doc ? &doc->diags : nullptr;
    if (!session.Loaded() && !view->Fault()) (void)view->Load(*m_renderer, *m_upload, diags);
    if (view->Fault()) {
        ShowFault(*view->Fault());
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1.0f || avail.y < 1.0f) return;

    session.Tick(ui.dt);

    const float scale = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.x);
    if (!EnsureTarget(ui, Pixels(avail.x, scale), Pixels(avail.y, scale))) {
        ShowFault(*m_fault);
        return;
    }

    constexpr ImGuiButtonFlags buttons =
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle;
    ImGui::InvisibleButton("##scene", avail, buttons);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();

    Render::Camera& camera = session.Camera();
    Render::View frameView;
    frameView.view = camera.ViewMatrix();
    frameView.proj = camera.Proj(avail.x / avail.y);
    frameView.mode = session.Mode();
    const ImVec4 clear = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    frameView.clear = glm::vec4(clear.x, clear.y, clear.z, 1.0f);
    auto drawn = m_renderer->Draw(*ui.frame, *m_target, session.Scene(), frameView, session.Overlays(), diags);
    if (!drawn.ok()) {
        m_fault = drawn.error();
        ShowFault(*m_fault);
        return;
    }

    m_fault.reset();

    ImDrawList* list = ImGui::GetWindowDrawList();
    list->PushClipRect(min, max, true);
    list->AddImage(m_texture, min, max);
    const ImVec2 center(max.x - kGizmoMargin - kGizmoRadius, max.y - kGizmoMargin - kGizmoRadius);
    const GizmoHit gizmo = DrawAxisGizmo(list, center, kGizmoRadius, camera.ViewRotation());
    list->PopClipRect();

    const ImGuiIO& io = ImGui::GetIO();
    bool interacting = false;
    if (gizmo.view && hovered) {
        camera.SnapToView(*gizmo.view);
        interacting = true;
    }
    const bool moved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
    if (active && !gizmo.hovered && moved) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            camera.ProcessMouseDrag(io.MouseDelta.x, io.MouseDelta.y);
            interacting = true;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            camera.ProcessMousePan(io.MouseDelta.x, io.MouseDelta.y);
            interacting = true;
        }
    }
    if (hovered && !gizmo.hovered && io.MouseWheel != 0.0f) {
        camera.ProcessScroll(io.MouseWheel);
        interacting = true;
    }
    if ((hovered || active) && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F)) {
            camera.FocusOn(session.Scene().Bounds());
            interacting = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
            session.Mode() = session.Mode() == Render::ShadingMode::Solid ? Render::ShadingMode::Textured
                                                                          : Render::ShadingMode::Solid;
            interacting = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_G)) {
            session.Overlays().grid = !session.Overlays().grid;
            interacting = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad5)) {
            camera.Reset();
            interacting = true;
        }
    }

    if (interacting || session.Animating()) ui.clock.RequestRedraw();
}

}
