#pragma once

// See VkContext.h for the binding include-order rule (volk.h, then
// vk_mem_alloc.h, before any other Vulkan-touching header). VkContext.h
// already pulls both in, in that order, so including it first here keeps
// the rule honored without repeating it.
#include <Onyx/RenderVk/VkContext.h>

#include <cstdint>
#include <functional>
#include <string>

namespace Onyx::RenderVk {

/// A VMA-backed VkBuffer. Default-constructed (buf == VK_NULL_HANDLE) is
/// both the "no resource" state Destroy() leaves behind and what
/// CreateBuffer() returns on failure.
struct Buffer {
    VkBuffer      buf   = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkDeviceSize  size  = 0;
};

/// A VMA-backed VkImage plus a VkImageView covering its one mip / one layer
/// -- every T2..T10 consumer wants exactly that view, so it is created
/// alongside the image rather than left to each caller.
struct Image2D {
    VkImage       img    = VK_NULL_HANDLE;
    VmaAllocation alloc  = VK_NULL_HANDLE;
    VkImageView   view   = VK_NULL_HANDLE;
    VkFormat      format = VK_FORMAT_UNDEFINED;
    uint32_t      width  = 0;
    uint32_t      height = 0;
};

/// Stateless GPU resource helpers -- every method takes the VkContext it
/// operates on rather than owning one. Buffer/Image2D are plain structs the
/// caller owns; each must be passed to the matching Destroy() exactly once.
class Resources {
public:
    Resources() = delete;

    /// Creates a VMA-backed buffer. Returns a default (buf ==
    /// VK_NULL_HANDLE) Buffer and fills err on failure.
    static Buffer CreateBuffer(VkContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                                VmaMemoryUsage memoryUsage, std::string& err);

    /// Creates a 2D image (1 mip, 1 layer, VK_IMAGE_TILING_OPTIMAL,
    /// VK_SHARING_MODE_EXCLUSIVE, device-local) plus its full-resource
    /// image view. Layout starts VK_IMAGE_LAYOUT_UNDEFINED --
    /// UploadImage() is what transitions it. Returns a default (img ==
    /// VK_NULL_HANDLE) Image2D and fills err on failure.
    static Image2D CreateImage2D(VkContext& ctx, uint32_t width, uint32_t height, VkFormat format,
                                  VkImageUsageFlags usage, VkSampleCountFlagBits samples,
                                  std::string& err);

    /// Staged upload: allocates a transient host-visible staging buffer,
    /// memcpy's `data` (size bytes) into it, then copies it into `dst` via
    /// a OneShot command scope. size must not exceed dst.size. dst must
    /// have been created with VK_BUFFER_USAGE_TRANSFER_DST_BIT.
    static bool Upload(VkContext& ctx, Buffer& dst, const void* data, VkDeviceSize size,
                        std::string& err);

    /// Staged upload of a tightly-packed RGBA8 image sized
    /// dst.width * dst.height * 4 bytes. Transitions dst
    /// UNDEFINED -> TRANSFER_DST_OPTIMAL (copy) -> SHADER_READ_ONLY_OPTIMAL
    /// via vkCmdPipelineBarrier2 (synchronization2); dst is left in
    /// SHADER_READ_ONLY_OPTIMAL on success. dst must have been created with
    /// VK_IMAGE_USAGE_TRANSFER_DST_BIT (and typically SAMPLED_BIT).
    static bool UploadImage(VkContext& ctx, Image2D& dst, const void* rgba, std::string& err);

    /// Destroys the buffer/image (no-op on an already-default value) and
    /// resets it to its default state.
    static void Destroy(VkContext& ctx, Buffer& buffer);
    static void Destroy(VkContext& ctx, Image2D& image);

    /// One-shot command scope: allocates a transient primary command
    /// buffer on the graphics queue, invokes `record` to fill it, submits,
    /// and blocks until the GPU finishes. Every staged upload above is
    /// built on this; T4/T5/T10 reuse it directly for their own one-off
    /// GPU work.
    static bool OneShot(VkContext& ctx, const std::function<void(VkCommandBuffer)>& record,
                         std::string& err);
};

} // namespace Onyx::RenderVk
