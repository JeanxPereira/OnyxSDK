#pragma once

#include <cstdint>
#include <memory>

#include <volk.h>

#include <imgui.h>

#include "Onyx/Core/Result.h"
#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/Handles.h"
#include "Onyx/Ui/TextureBinder.h"

// Error codes: ui.backend.init  there is no current ImGui context, ImGui_ImplVulkan_Init refused the
// device, a texture descriptor set could not be allocated, or Fault reports a failed Vulkan call.
namespace Onyx::Ui {

struct BackendOptions {
    // The format of the colour attachment the draw data is recorded into.
    VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    uint32_t imageCount = 2;
};

// ImGui's Vulkan renderer over dynamic rendering on an Onyx Gpu::Device. It binds to the ImGui
// context that is current when Create runs and must be destroyed while that context is still current.
// Font textures are managed by ImGui's texture protocol (ImGuiBackendFlags_RendererHasTextures): imgui
// uploads them by submitting to the graphics queue directly and waiting for it idle. Record and
// RebuildFonts hold Gpu::Queue::Lock around those calls, so a submission from another thread through
// Queue::Submit waits rather than racing them; Record and RebuildFonts must not be called while the
// caller holds that lock.
// Under IMGUI_IMPL_VULKAN_USE_VOLK imgui resolves Vulkan through volk's process-wide globals, so a host
// that creates the Device with Device::Adopt must have volk loaded for the same instance (see the note on
// volk globals in Device.h).
// Must be destroyed before its Device. Not thread-safe.
class ImGuiVulkanBackend : public ITextureBinder {
public:
    static Result<std::unique_ptr<ImGuiVulkanBackend>> Create(Gpu::Device& device, const BackendOptions& options);
    ~ImGuiVulkanBackend() override;

    ImGuiVulkanBackend(const ImGuiVulkanBackend&) = delete;
    ImGuiVulkanBackend& operator=(const ImGuiVulkanBackend&) = delete;

    void NewFrame();
    // Records the draw data into whatever dynamic rendering the command buffer has begun.
    void Record(VkCommandBuffer cmd, ImDrawData* drawData);
    // Destroys the atlas textures after the queue drains and asks ImGui to recreate them on the next
    // frame, for use after the font atlas changed.
    void RebuildFonts();

    // The sampler argument is ignored: imgui's Vulkan backend samples every texture with its internal
    // linear sampler.
    Result<ImTextureID> Bind(VkImageView view, VkImageLayout layout, VkSampler sampler) override;
    // Frees the descriptor set once the queue completes the submissions recorded so far. The free runs
    // through ImGui_ImplVulkan_RemoveTexture when the device's retire queue collects it, against whatever
    // ImGui context is current then, so collect while this backend's context is current.
    void Unbind(ImTextureID id) override;
    // Rewrites the descriptor set behind the id in place; fails with ui.backend.init for a null id.
    Result<void> Rebind(ImTextureID id, VkImageView view, VkImageLayout layout) override;

    // The first Vulkan failure imgui reported through its result callback since the last call
    // (ui.backend.init, with the VkResult name in the message), or success. Reading clears it; check it after
    // Record and RebuildFonts.
    Result<void> Fault() const;

private:
    explicit ImGuiVulkanBackend(Gpu::Device& device) : m_device(device) {}

    Gpu::Device& m_device;
};

}
