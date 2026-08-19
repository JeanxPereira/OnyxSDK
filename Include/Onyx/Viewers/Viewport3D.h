#pragma once
#include <Onyx/Viewers/IDocumentContent.h>
#include <Onyx/Rendering/Camera.h>
#include <Onyx/Rendering/AxisGizmo.h>
#include <Onyx/Rendering/SceneRenderer.h> // Rendering::RenderBatch/ShadingMode -- see .cpp top comment
#include <Onyx/Parsers/MeshData.h>
#include <Onyx/Parsers/TextureData.h>
#include <Onyx/Parsers/SceneNode.h>
#include <Onyx/Domain/BoundingBox.h>
#include <string>
#include <vector>
#include <memory>
#include <imgui.h>

// Forward-declared only, never included here -- Onyx::RenderVk::VkContext
// (and every other Vulkan-touching type this class needs: ScenePipelines,
// OffscreenTarget, SceneRendererVk, ...) pulls in volk.h, which on Windows
// pulls in <windows.h> for VK_USE_PLATFORM_WIN32_KHR. Viewport3D.h is a
// widely-included header (DocumentBrowser.cpp, CameraPanel.cpp, ...) that
// has NOTHING to do with Vulkan from its callers' point of view -- pulling
// windows.h in through it broke the build the first time this was tried
// (<wingdi.h>'s `#define TextOut TextOutW/A` collided with
// Onyx::Modules::TextOut, a real type DocumentBrowser.cpp names). Every
// Vulkan-touching member lives instead in the private, .cpp-only
// `VulkanState` struct below -- the same forward-declare + unique_ptr<
// incomplete-type> pattern Include/Onyx/App/Window.h already established
// for exactly this reason (see that header's own top comment).
namespace Onyx::RenderVk { class VkContext; }
namespace Onyx::App { class TexturePool; }

namespace Onyx::Viewers {

// T10: renders through Onyx::RenderVk::SceneRendererVk into a VkContext-
// owned OffscreenTarget, replacing the GL FBO pair + Onyx::Rendering::
// SceneRenderer this class used before T9 removed the GL context Window
// keeps current. See Source/Viewers/Viewport3D.cpp's top comment for the
// full design writeup (recording seam, bounds computation, and the
// animation/LOD-toggle/outline features this milestone's SceneRendererVk
// does not yet implement).
class Viewport3D : public IDocumentContent {
public:
    Viewport3D(const std::string& name);
    ~Viewport3D() override;

    std::string GetName() const override;
    void Draw() override;
    void DrawInspector() override;
    Viewport3D* GetEmbeddedViewport() override { return this; }

    // Load mesh data into the viewport (routes through SceneRenderer)
    void LoadFromMeshData(const Parsers::MeshData& data, const std::vector<std::unique_ptr<Parsers::TextureData>>& textures = {});
    void LoadScene(std::unique_ptr<Parsers::SceneData> scene);
    void ClearScene();

    // Accessors for the CameraPanel (renders camera tuning UI as a dock tab
    // next to Inspector).
    Rendering::Camera&            GetCamera()  { return m_camera; }
    int                GetFboWidth()  const { return m_fboWidth; }
    int                GetFboHeight() const { return m_fboHeight; }
    void               RequestRedraw()       { m_needsRedraw = true; }

    // Render settings
    bool showGrid       = true;
    bool showOutline    = true;
    bool showBones      = false;
    bool showObjectList = true;
    Rendering::ShadingMode shadingMode = Rendering::ShadingMode::Solid;

    // Outline settings (SceneRendererVk does not implement the highlight
    // outline pass this milestone -- see .cpp top comment; kept as plain
    // state so the inspector/toolbar this class already had keep compiling
    // and the setting round-trips once a future task wires it up).
    glm::vec4 outlineColor      {0.0f, 0.0f, 0.0f, 1.0f};
    float     outlineThickness  = 0.015f;

    // Background gradient
    glm::vec3 bgTopColor    {0.18f, 0.18f, 0.22f};
    glm::vec3 bgBottomColor {0.08f, 0.08f, 0.10f};

private:
    void EnsureVulkanReady();
    void ResizeTarget(int width, int height);
    void RenderFrame(int width, int height);
    void DrawToolbar(ImVec2 avail, ImVec2 cursorPos);
    void HandleInput();
    void ComputeBounds();
    bool HasBatches() const; // true once SceneRendererVk::Build() produced >=1 batch

    std::string m_name;
    Rendering::Camera m_camera;
    Rendering::AxisGizmo m_axisGizmo;

    // Keep scene data around: bounds computation, HasSkeleton()/IsEmpty(),
    // and the animations-present check the inspector shows (M4 gap: no
    // playback -- see .cpp top comment).
    std::shared_ptr<Parsers::SceneData> m_sceneData;
    Onyx::Domain::BoundingBox m_bounds;

    // ── Vulkan (T10) -- see this file's top comment for why this is a
    // forward-declared, .cpp-only struct rather than plain members ───────
    struct VulkanState;
    std::unique_ptr<VulkanState> m_vk;
    Onyx::RenderVk::VkContext* m_ctx = nullptr; // non-owning, Onyx::RenderVk::GetGlobalContext()
    bool m_vkReady = false; // VulkanState's pipelines created successfully
    std::unique_ptr<Onyx::App::TexturePool> m_texPool;
    ImTextureID m_displayTexId = 0;

    int m_fboWidth = 0;
    int m_fboHeight = 0;

    bool m_needsRedraw = true;
    bool m_viewportHovered = false;
    float m_lastFrameTime = 0.0f; // for Camera::UpdateAnimation's dt (view-flight easing, not mesh animation)

    // Photoshop-style click-and-drag toggling across visibility checkboxes
    bool m_dragToggleActive = false;
    bool m_dragToggleValue  = false;
};

} // namespace Onyx::Viewers
