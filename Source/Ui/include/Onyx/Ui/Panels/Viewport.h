#pragma once

#include <Onyx/Core/Result.h>
#include <Onyx/Ui/Panel.h>

#include <imgui.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace Onyx::Gpu {
class Device;
class RenderTarget;
class UploadContext;
}

namespace Onyx::Render {
class SceneRenderer;
}

namespace Onyx::Ui {

class TexturePool;

// The 3D view of the active scene tab. It owns the scene renderer and an upload context (both made
// on the first draw that has a device) and a render target sized to the panel, shown through one
// texture of the UI's pool. The scene is recorded into UiContext::frame, so the host must have begun
// the frame before drawing panels and must record the ImGui pass into the same frame's command buffer
// after the panels drew, so the target is rendered before the UI samples it. Orbit with the right button, pan with the middle one, zoom with
// the wheel; F frames the scene, Z flips the shading, G the grid, keypad 5 resets the camera.
//
// Without a device or a frame it draws a placeholder. It removes its texture from the pool on
// destruction, so the pool must outlive it; the device must outlive both.
class Viewport : public IPanel {
public:
    Viewport();
    ~Viewport() override;

    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;

    std::string_view Id() const override { return "onyx.viewport"; }
    std::string_view Title() const override { return "Viewport"; }
    void Draw(UiContext& ui) override;

    // Zero before the first draw that made a target.
    uint64_t TargetGeneration() const;
    const std::optional<Error>& Fault() const { return m_fault; }

private:
    bool EnsureDevice(UiContext& ui);
    bool EnsureTarget(UiContext& ui, uint32_t width, uint32_t height);

    std::unique_ptr<Render::SceneRenderer> m_renderer;
    std::unique_ptr<Gpu::UploadContext> m_upload;
    std::unique_ptr<Gpu::RenderTarget> m_target;
    TexturePool* m_pool = nullptr;
    ImTextureID m_texture = ImTextureID_Invalid;
    uint64_t m_registered = 0;
    std::optional<Error> m_fault;
    Gpu::Device* m_failedDevice = nullptr;
};

}
