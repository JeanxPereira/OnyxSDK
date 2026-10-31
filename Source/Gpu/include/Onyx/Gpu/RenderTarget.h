#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <volk.h>
#include <glm/vec4.hpp>

#include "Onyx/Core/Result.h"
#include "Onyx/Gpu/Handles.h"
#include "Onyx/Gpu/Retire.h"

namespace Onyx::Gpu {

class Device;

struct RenderTargetDesc {
    VkExtent2D extent{};
    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    // VK_FORMAT_UNDEFINED renders without a depth attachment.
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_4_BIT;
};

// Multisampled colour and depth attachments resolved into a single-sample image that shaders sample.
// With one sample the resolve image is rendered to directly.
class RenderTarget {
public:
    // gpu.extent when either dimension is zero.
    static Result<std::unique_ptr<RenderTarget>> Create(Device& device, const RenderTargetDesc& desc);

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    // gpu.extent on a zero dimension. The previous images retire through Unique and are destroyed
    // once the queue completes the submissions that may still use them. Old images retire at
    // LastSubmitted, so Resize must not fall between a host's Begin and the submit of that command buffer.
    Result<void> Resize(VkExtent2D extent);

    // Transitions the attachments, begins dynamic rendering with a clear and sets viewport and scissor.
    void Begin(VkCommandBuffer cmd, const glm::vec4& clear);
    // Ends rendering and leaves the resolve image in SHADER_READ_ONLY_OPTIMAL.
    void End(VkCommandBuffer cmd);

    VkImageView ResolveView() const { return m_resolve.view.Get(); }
    VkImage ResolveImage() const { return m_resolve.handle.Get(); }
    VkExtent2D Extent() const { return m_desc.extent; }
    const RenderTargetDesc& Desc() const { return m_desc; }
    // Increments on every Resize that changed the images; a host keys cached descriptors on it.
    uint64_t Generation() const { return m_generation; }

private:
    RenderTarget(Device& device, const RenderTargetDesc& desc) : m_device(device), m_desc(desc) {}
    Result<void> CreateImages(VkExtent2D extent);

    Device& m_device;
    RenderTargetDesc m_desc;
    Image m_color;
    Image m_depth;
    Image m_resolve;
    uint64_t m_generation = 0;
};

// Synchronous and meant for headless runs and tests: copies the resolve image to a host buffer after
// the submission that wrote it (`after`, a value on the graphics queue timeline) and returns tightly
// packed RGBA8 rows, top row first. Supports 8-bit RGBA and BGRA colour formats and fails with
// gpu.format for any other. The resolve image is read in SHADER_READ_ONLY_OPTIMAL, so call it only
// after at least one End of a frame that drew into the target.
Result<std::vector<std::byte>> ReadbackColor(Device& device, RenderTarget& target, FenceValue after);

}
