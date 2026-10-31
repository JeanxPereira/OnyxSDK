#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#include <volk.h>

#include <imgui.h>

#include "Onyx/Core/Result.h"
#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/Handles.h"
#include "Onyx/Ui/TextureBinder.h"

// Error codes: ui.backend.init  there is no current ImGui context, the sampler could not be created or
// ImGui_ImplVulkan_Init refused the device; ui.backend.bind  the descriptor set for a texture could
// not be allocated.
namespace Onyx::Ui {

struct BackendOptions {
    // The format of the colour attachment the draw data is recorded into.
    VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    uint32_t imageCount = 2;
};

// ImGui's Vulkan renderer over dynamic rendering on an Onyx Gpu::Device. It binds to the ImGui
// context that is current when Create runs and must be destroyed while that context is still current.
// Font textures are managed by ImGui's texture protocol (ImGuiBackendFlags_RendererHasTextures); the
// backend uploads them with its own queue submissions, so drive it from the thread that owns the queue.
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

    // The sampler argument is ignored: the backend draws every texture with its own linear sampler.
    Result<ImTextureID> Bind(VkImageView view, VkImageLayout layout, VkSampler sampler) override;
    // Frees the descriptor set once the queue completes the submissions recorded so far.
    void Unbind(ImTextureID id) override;

    VkSampler DefaultSampler() const { return m_sampler.Get(); }

private:
    ImGuiVulkanBackend(Gpu::Device& device, Gpu::Unique<VkSampler> sampler)
        : m_device(device), m_sampler(std::move(sampler)) {}

    Gpu::Device& m_device;
    Gpu::Unique<VkSampler> m_sampler;
};

}
