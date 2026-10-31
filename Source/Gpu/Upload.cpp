#include "Onyx/Gpu/Upload.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>

#include "VkError.h"

namespace Onyx::Gpu {

namespace {

constexpr VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

Error SizeError(std::string message) {
    return {"gpu.upload.size", std::move(message), 0};
}

VkFormat ToVkFormat(Domain::PixelFormat format) {
    switch (format) {
    case Domain::PixelFormat::Rgba8: return VK_FORMAT_R8G8B8A8_UNORM;
    case Domain::PixelFormat::Bc1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case Domain::PixelFormat::Bc3: return VK_FORMAT_BC3_UNORM_BLOCK;
    case Domain::PixelFormat::Bc4: return VK_FORMAT_BC4_UNORM_BLOCK;
    case Domain::PixelFormat::Bc5: return VK_FORMAT_BC5_UNORM_BLOCK;
    case Domain::PixelFormat::Bc7: return VK_FORMAT_BC7_UNORM_BLOCK;
    case Domain::PixelFormat::RgbaF16: return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

void ImageBarrier(const Device& device, VkCommandBuffer cmd, VkImage image, uint32_t baseMip, uint32_t mipCount,
                  VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                  VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, 0, 1};
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    device.Fn().vkCmdPipelineBarrier2(cmd, &dependency);
}

void BufferBarrier(const Device& device, VkCommandBuffer cmd, VkBuffer buffer, VkPipelineStageFlags2 srcStage,
                   VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &barrier;
    device.Fn().vkCmdPipelineBarrier2(cmd, &dependency);
}

}

Result<std::unique_ptr<UploadContext>> UploadContext::Create(Device& device, const UploadOptions& options) {
    using R = Result<std::unique_ptr<UploadContext>>;
    if (options.stagingBytes == 0) {
        return R::Fail(SizeError("stagingBytes must be greater than zero"));
    }
    std::unique_ptr<UploadContext> context(new UploadContext(device));

    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    device.InstanceFn().getPhysicalDeviceProperties2(device.PhysicalDevice(), &properties);
    context->m_copyAlignment = std::max<VkDeviceSize>(
        16, std::bit_ceil(std::max<VkDeviceSize>(1, properties.properties.limits.optimalBufferCopyOffsetAlignment)));

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = device.GraphicsQueue().Family();
    VkCommandPool pool = VK_NULL_HANDLE;
    VkResult r = device.Fn().vkCreateCommandPool(device.Handle(), &poolInfo, nullptr, &pool);
    if (r != VK_SUCCESS) {
        return R::Fail(VkFail("gpu.vk", "vkCreateCommandPool", r));
    }
    context->m_pool = Unique<VkCommandPool>(&device, pool);

    auto staging = CreateBuffer(device, options.stagingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                MemoryUsage::HostToDevice);
    if (!staging.ok()) {
        return R::Fail(staging.error());
    }
    if (staging.value().mapped == nullptr) {
        return R::Fail({"gpu.alloc", "the staging buffer is not host mapped", 0});
    }
    context->m_staging = std::move(staging.value());
    return R::Ok(std::move(context));
}

UploadContext::~UploadContext() = default;

void UploadContext::Reap() {
    const FenceValue completed = m_device.GraphicsQueue().Completed();
    std::erase_if(m_inFlight, [&](InFlight& entry) {
        if (entry.value > completed) {
            return false;
        }
        m_device.Fn().vkFreeCommandBuffers(m_device.Handle(), m_pool.Get(), 1, &entry.cmd);
        return true;
    });
}

Result<VkCommandBuffer> UploadContext::Recording() {
    if (m_cmd != VK_NULL_HANDLE) {
        return Result<VkCommandBuffer>::Ok(m_cmd);
    }
    Reap();
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_pool.Get();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkResult r = m_device.Fn().vkAllocateCommandBuffers(m_device.Handle(), &allocInfo, &cmd);
    if (r != VK_SUCCESS) {
        return Result<VkCommandBuffer>::Fail(VkFail("gpu.vk", "vkAllocateCommandBuffers", r));
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r = m_device.Fn().vkBeginCommandBuffer(cmd, &begin);
    if (r != VK_SUCCESS) {
        m_device.Fn().vkFreeCommandBuffers(m_device.Handle(), m_pool.Get(), 1, &cmd);
        return Result<VkCommandBuffer>::Fail(VkFail("gpu.vk", "vkBeginCommandBuffer", r));
    }
    m_cmd = cmd;
    return Result<VkCommandBuffer>::Ok(cmd);
}

Result<UploadContext::Staged> UploadContext::Stage(VkDeviceSize size) {
    using R = Result<Staged>;
    if (size > m_staging.size) {
        auto oneOff = CreateBuffer(m_device, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::HostToDevice);
        if (!oneOff.ok()) {
            return R::Fail(oneOff.error());
        }
        Staged staged{oneOff.value().handle.Get(), 0, oneOff.value().mapped, oneOff.value().allocation};
        m_oneOff.push_back(std::move(oneOff.value()));
        return R::Ok(staged);
    }
    VkDeviceSize offset = AlignUp(m_stagingOffset, m_copyAlignment);
    if (offset + size > m_staging.size) {
        // The copies recorded so far still read the ring: submit them and wait before reusing it.
        auto flushed = Flush();
        if (!flushed.ok()) {
            return R::Fail(flushed.error());
        }
        if (flushed.value() != 0) {
            auto waited = m_device.GraphicsQueue().Wait(flushed.value());
            if (!waited.ok()) {
                return R::Fail(waited.error());
            }
        }
        offset = 0;
    }
    m_stagingOffset = offset + size;
    return R::Ok(Staged{m_staging.handle.Get(), offset, static_cast<std::byte*>(m_staging.mapped) + offset,
                        m_staging.allocation});
}

Result<Buffer> UploadContext::Upload(std::span<const std::byte> bytes, VkBufferUsageFlags usage) {
    if (bytes.empty()) {
        return Result<Buffer>::Fail(SizeError("cannot upload an empty buffer"));
    }
    auto staged = Stage(bytes.size());
    if (!staged.ok()) {
        return Result<Buffer>::Fail(staged.error());
    }
    auto buffer = CreateBuffer(m_device, bytes.size(), usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               MemoryUsage::DeviceLocal);
    if (!buffer.ok()) {
        return buffer;
    }
    auto cmd = Recording();
    if (!cmd.ok()) {
        return Result<Buffer>::Fail(cmd.error());
    }
    std::memcpy(staged.value().mapped, bytes.data(), bytes.size());
    VkResult r = vmaFlushAllocation(m_device.Allocator(), staged.value().allocation, staged.value().offset,
                                    bytes.size());
    if (r != VK_SUCCESS) {
        return Result<Buffer>::Fail(VkFail("gpu.vk", "vmaFlushAllocation", r));
    }

    VkBufferCopy region{staged.value().offset, 0, bytes.size()};
    m_device.Fn().vkCmdCopyBuffer(cmd.value(), staged.value().buffer, buffer.value().handle.Get(), 1, &region);
    BufferBarrier(m_device, cmd.value(), buffer.value().handle.Get(), VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                  VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    return buffer;
}

Result<Image> UploadContext::Upload(const Domain::Texture& texture) {
    if (texture.width == 0 || texture.height == 0) {
        return Result<Image>::Fail(SizeError(std::format("texture '{}' has a zero dimension", texture.name)));
    }
    const size_t expected = Domain::ExpectedByteSize(texture.format, texture.width, texture.height);
    if (texture.pixels.size() != expected) {
        return Result<Image>::Fail(SizeError(std::format("texture '{}' holds {} bytes, expected {} for {}x{}",
                                                         texture.name, texture.pixels.size(), expected, texture.width,
                                                         texture.height)));
    }

    const VkFormat format = ToVkFormat(texture.format);
    const bool mipped = texture.format == Domain::PixelFormat::Rgba8;
    VkFormatProperties props{};
    m_device.InstanceFn().getPhysicalDeviceFormatProperties(m_device.PhysicalDevice(), format, &props);
    VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (mipped) {
        needed |= VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    }
    if ((props.optimalTilingFeatures & needed) != needed) {
        return Result<Image>::Fail({"gpu.upload.format",
                                    std::format("texture '{}': format {} is not supported for sampling and transfer",
                                                texture.name, static_cast<int>(format)),
                                    0});
    }

    const uint32_t levels = mipped ? static_cast<uint32_t>(std::bit_width(std::max(texture.width, texture.height))) : 1;

    ImageDesc desc;
    desc.extent = {texture.width, texture.height};
    desc.format = format;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 (mipped ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    desc.mipLevels = levels;
    auto image = CreateImage(m_device, desc);
    if (!image.ok()) {
        return image;
    }
    auto recorded = RecordTexture(texture, image.value().handle.Get(), levels, false);
    if (!recorded.ok()) {
        return Result<Image>::Fail(recorded.error());
    }
    return image;
}

Result<void> UploadContext::UploadInto(const Domain::Texture& texture, const Image& image) {
    if (!image.handle || image.extent.width != texture.width || image.extent.height != texture.height ||
        image.format != ToVkFormat(texture.format)) {
        return Result<void>::Fail(
            SizeError(std::format("texture '{}' does not match the extent and format of the target image", texture.name)));
    }
    const size_t expected = Domain::ExpectedByteSize(texture.format, texture.width, texture.height);
    if (texture.pixels.size() != expected) {
        return Result<void>::Fail(SizeError(std::format("texture '{}' holds {} bytes, expected {} for {}x{}",
                                                        texture.name, texture.pixels.size(), expected, texture.width,
                                                        texture.height)));
    }
    return RecordTexture(texture, image.handle.Get(), image.mipLevels, true);
}

// Stages the pixels and records the copy, the mip blits and the final transition. `overwrite` names an
// image that earlier submissions may still read or write: its first barrier then waits for them.
Result<void> UploadContext::RecordTexture(const Domain::Texture& texture, VkImage handle, uint32_t levels,
                                          bool overwrite) {
    auto staged = Stage(texture.pixels.size());
    if (!staged.ok()) {
        return Result<void>::Fail(staged.error());
    }
    auto recording = Recording();
    if (!recording.ok()) {
        return Result<void>::Fail(recording.error());
    }
    const VkCommandBuffer cmd = recording.value();

    std::memcpy(staged.value().mapped, texture.pixels.data(), texture.pixels.size());
    VkResult r = vmaFlushAllocation(m_device.Allocator(), staged.value().allocation, staged.value().offset,
                                    texture.pixels.size());
    if (r != VK_SUCCESS) {
        return Result<void>::Fail(VkFail("gpu.vk", "vmaFlushAllocation", r));
    }

    const auto& fn = m_device.Fn();
    constexpr auto kTransfer = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    constexpr auto kReadStages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    ImageBarrier(m_device, cmd, handle, 0, levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 overwrite ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_2_NONE, 0, kTransfer,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkBufferImageCopy copy{};
    copy.bufferOffset = staged.value().offset;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {texture.width, texture.height, 1};
    fn.vkCmdCopyBufferToImage(cmd, staged.value().buffer, handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    int32_t width = static_cast<int32_t>(texture.width);
    int32_t height = static_cast<int32_t>(texture.height);
    for (uint32_t level = 1; level < levels; ++level) {
        ImageBarrier(m_device, cmd, handle, level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, kTransfer,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
        const int32_t nextWidth = std::max(1, width / 2);
        const int32_t nextHeight = std::max(1, height / 2);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
        blit.srcOffsets[1] = {width, height, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        blit.dstOffsets[1] = {nextWidth, nextHeight, 1};
        fn.vkCmdBlitImage(cmd, handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, handle,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        ImageBarrier(m_device, cmd, handle, level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kTransfer, VK_ACCESS_2_TRANSFER_READ_BIT, kReadStages,
                     VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        width = nextWidth;
        height = nextHeight;
    }
    ImageBarrier(m_device, cmd, handle, levels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kTransfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, kReadStages,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    return Result<void>::Ok();
}

Result<FenceValue> UploadContext::Flush() {
    if (m_cmd == VK_NULL_HANDLE) {
        return Result<FenceValue>::Ok(m_lastFlushed);
    }
    const auto& fn = m_device.Fn();
    const VkCommandBuffer cmd = m_cmd;
    m_cmd = VK_NULL_HANDLE;
    std::vector<Buffer> oneOff = std::move(m_oneOff);
    m_oneOff.clear();

    VkResult ended = fn.vkEndCommandBuffer(cmd);
    if (ended != VK_SUCCESS) {
        fn.vkFreeCommandBuffers(m_device.Handle(), m_pool.Get(), 1, &cmd);
        return Result<FenceValue>::Fail(VkFail("gpu.vk", "vkEndCommandBuffer", ended));
    }
    SubmitDesc desc;
    desc.commandBuffers = std::span<const VkCommandBuffer>(&cmd, 1);
    auto value = m_device.GraphicsQueue().Submit(desc);
    if (!value.ok()) {
        fn.vkFreeCommandBuffers(m_device.Handle(), m_pool.Get(), 1, &cmd);
        return value;
    }
    m_lastFlushed = value.value();
    m_inFlight.push_back({cmd, value.value(), std::move(oneOff)});
    return value;
}

Result<std::vector<std::byte>> UploadContext::Download(const Buffer& buffer, VkDeviceSize size) {
    using R = Result<std::vector<std::byte>>;
    if (!buffer.handle || size == 0 || size > buffer.size) {
        return R::Fail(SizeError(std::format("cannot download {} bytes from a buffer of {} bytes", size, buffer.size)));
    }
    auto flushed = Flush();
    if (!flushed.ok()) {
        return R::Fail(flushed.error());
    }
    auto host = CreateBuffer(m_device, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryUsage::DeviceToHost);
    if (!host.ok()) {
        return R::Fail(host.error());
    }
    if (host.value().mapped == nullptr) {
        return R::Fail({"gpu.alloc", "the readback buffer is not host mapped", 0});
    }
    auto recording = Recording();
    if (!recording.ok()) {
        return R::Fail(recording.error());
    }
    const VkCommandBuffer cmd = recording.value();
    m_cmd = VK_NULL_HANDLE;
    const auto& fn = m_device.Fn();

    BufferBarrier(m_device, cmd, buffer.handle.Get(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                  VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferCopy region{0, 0, size};
    fn.vkCmdCopyBuffer(cmd, buffer.handle.Get(), host.value().handle.Get(), 1, &region);
    BufferBarrier(m_device, cmd, host.value().handle.Get(), VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);

    VkResult r = fn.vkEndCommandBuffer(cmd);
    if (r != VK_SUCCESS) {
        fn.vkFreeCommandBuffers(m_device.Handle(), m_pool.Get(), 1, &cmd);
        return R::Fail(VkFail("gpu.vk", "vkEndCommandBuffer", r));
    }
    SubmitDesc desc;
    desc.commandBuffers = std::span<const VkCommandBuffer>(&cmd, 1);
    auto value = m_device.GraphicsQueue().Submit(desc);
    if (!value.ok()) {
        fn.vkFreeCommandBuffers(m_device.Handle(), m_pool.Get(), 1, &cmd);
        return R::Fail(value.error());
    }
    m_lastFlushed = value.value();
    auto waited = m_device.GraphicsQueue().Wait(value.value());
    if (!waited.ok()) {
        m_inFlight.push_back({cmd, value.value(), {}});
        return R::Fail(waited.error());
    }
    fn.vkFreeCommandBuffers(m_device.Handle(), m_pool.Get(), 1, &cmd);
    r = vmaInvalidateAllocation(m_device.Allocator(), host.value().allocation, 0, size);
    if (r != VK_SUCCESS) {
        return R::Fail(VkFail("gpu.vk", "vmaInvalidateAllocation", r));
    }
    std::vector<std::byte> out(size);
    std::memcpy(out.data(), host.value().mapped, size);
    return R::Ok(std::move(out));
}

}
