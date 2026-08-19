#include <Onyx/RenderVk/VkResources.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Onyx::Rendering {

namespace {

// The legacy (but still functional in VMA 3.x) usage values that name a
// host-visible allocation explicitly. CreateBuffer maps these to the
// HOST_ACCESS + MAPPED allocation flags every staging buffer in this file
// needs; anything else (GPU_ONLY, AUTO_PREFER_DEVICE, ...) is left device-
// local with no persistent mapping.
bool WantsHostAccess(VmaMemoryUsage usage) {
    switch (usage) {
        case VMA_MEMORY_USAGE_CPU_ONLY:
        case VMA_MEMORY_USAGE_CPU_TO_GPU:
        case VMA_MEMORY_USAGE_GPU_TO_CPU:
        case VMA_MEMORY_USAGE_CPU_COPY:
        case VMA_MEMORY_USAGE_AUTO_PREFER_HOST:
            return true;
        default:
            return false;
    }
}

// T2's only image caller uploads color data; depth formats are named here
// so a later task's CreateImage2D(D32_SFLOAT, ...) call (T4's
// OffscreenTarget) gets the right aspect mask without having to duplicate
// this switch. No combined depth/stencil format is listed -- none of this
// milestone's targets use one (D32_SFLOAT is the fixed depth format per the
// plan), so VK_IMAGE_ASPECT_STENCIL_BIT is intentionally never added here.
VkImageAspectFlags AspectMaskFor(VkFormat format) {
    switch (format) {
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

// T7 mip remedy: full chain down to a 1x1 base level, same formula every
// GPU API uses (floor(log2(max(w,h)))+1).
uint32_t MipLevelsFor(uint32_t width, uint32_t height) {
    return static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(std::max(width, height))))) + 1;
}

} // namespace

Buffer Resources::CreateBuffer(VkContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                                VmaMemoryUsage memoryUsage, std::string& err) {
    Buffer out{};
    if (size == 0) {
        err = "Resources::CreateBuffer: size is zero";
        return out;
    }

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    if (WantsHostAccess(memoryUsage)) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkResult vr = vmaCreateBuffer(ctx.Allocator(), &bufInfo, &allocInfo, &buffer, &alloc, nullptr);
    if (vr != VK_SUCCESS) {
        err = "vmaCreateBuffer failed (VkResult " + std::to_string(static_cast<int>(vr)) + ")";
        return out;
    }

    out.buf = buffer;
    out.alloc = alloc;
    out.size = size;
    return out;
}

Image2D Resources::CreateImage2D(VkContext& ctx, uint32_t width, uint32_t height, VkFormat format,
                                  VkImageUsageFlags usage, VkSampleCountFlagBits samples,
                                  std::string& err, bool generateMips) {
    Image2D out{};
    if (width == 0 || height == 0) {
        err = "Resources::CreateImage2D: width/height must be nonzero";
        return out;
    }

    const uint32_t mipLevels = generateMips ? MipLevelsFor(width, height) : 1;
    // Every mip level past 0 is filled by blitting FROM the level above it
    // (UploadImage's mip-gen chain), so the image itself must be a valid
    // blit source in addition to whatever the caller already asked for.
    const VkImageUsageFlags effectiveUsage =
        generateMips ? (usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) : usage;

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = format;
    imgInfo.extent = {width, height, 1};
    imgInfo.mipLevels = mipLevels;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = samples;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = effectiveUsage;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkResult vr = vmaCreateImage(ctx.Allocator(), &imgInfo, &allocInfo, &image, &alloc, nullptr);
    if (vr != VK_SUCCESS) {
        err = "vmaCreateImage failed (VkResult " + std::to_string(static_cast<int>(vr)) + ")";
        return out;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = AspectMaskFor(format);
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView view = VK_NULL_HANDLE;
    vr = vkCreateImageView(ctx.Device(), &viewInfo, nullptr, &view);
    if (vr != VK_SUCCESS) {
        err = "vkCreateImageView failed (VkResult " + std::to_string(static_cast<int>(vr)) + ")";
        vmaDestroyImage(ctx.Allocator(), image, alloc);
        return out;
    }

    out.img = image;
    out.alloc = alloc;
    out.view = view;
    out.format = format;
    out.width = width;
    out.height = height;
    out.mipLevels = mipLevels;
    return out;
}

bool Resources::Upload(VkContext& ctx, Buffer& dst, const void* data, VkDeviceSize size,
                       std::string& err) {
    if (dst.buf == VK_NULL_HANDLE) {
        err = "Resources::Upload: destination buffer is not created";
        return false;
    }
    if (size == 0) {
        err = "Resources::Upload: size is zero";
        return false;
    }
    if (size > dst.size) {
        err = "Resources::Upload: size exceeds destination buffer capacity";
        return false;
    }

    std::string stagingErr;
    Buffer staging = CreateBuffer(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VMA_MEMORY_USAGE_CPU_ONLY, stagingErr);
    if (staging.buf == VK_NULL_HANDLE) {
        err = "Resources::Upload: staging buffer: " + stagingErr;
        return false;
    }

    VmaAllocationInfo stagingInfo{};
    vmaGetAllocationInfo(ctx.Allocator(), staging.alloc, &stagingInfo);
    if (!stagingInfo.pMappedData) {
        err = "Resources::Upload: staging buffer is not host-mapped";
        Destroy(ctx, staging);
        return false;
    }
    std::memcpy(stagingInfo.pMappedData, data, static_cast<size_t>(size));

    bool ok = OneShot(ctx, [&](VkCommandBuffer cmd) {
        VkBufferCopy copy{};
        copy.srcOffset = 0;
        copy.dstOffset = 0;
        copy.size = size;
        vkCmdCopyBuffer(cmd, staging.buf, dst.buf, 1, &copy);
    }, err);

    Destroy(ctx, staging);
    return ok;
}

bool Resources::UploadImage(VkContext& ctx, Image2D& dst, const void* rgba, std::string& err) {
    if (dst.img == VK_NULL_HANDLE) {
        err = "Resources::UploadImage: destination image is not created";
        return false;
    }

    const VkDeviceSize size = static_cast<VkDeviceSize>(dst.width) * dst.height * 4;

    std::string stagingErr;
    Buffer staging = CreateBuffer(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VMA_MEMORY_USAGE_CPU_ONLY, stagingErr);
    if (staging.buf == VK_NULL_HANDLE) {
        err = "Resources::UploadImage: staging buffer: " + stagingErr;
        return false;
    }

    VmaAllocationInfo stagingInfo{};
    vmaGetAllocationInfo(ctx.Allocator(), staging.alloc, &stagingInfo);
    if (!stagingInfo.pMappedData) {
        err = "Resources::UploadImage: staging buffer is not host-mapped";
        Destroy(ctx, staging);
        return false;
    }
    std::memcpy(stagingInfo.pMappedData, rgba, static_cast<size_t>(size));

    const VkImageAspectFlags aspect = AspectMaskFor(dst.format);
    const uint32_t mipLevels = dst.mipLevels > 0 ? dst.mipLevels : 1;

    bool ok = OneShot(ctx, [&](VkCommandBuffer cmd) {
        auto barrier = [&](uint32_t baseMip, uint32_t levelCount, VkPipelineStageFlags2 srcStage,
                           VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                           VkAccessFlags2 dstAccess, VkImageLayout oldLayout, VkImageLayout newLayout) {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = srcStage;
            b.srcAccessMask = srcAccess;
            b.dstStageMask = dstStage;
            b.dstAccessMask = dstAccess;
            b.oldLayout = oldLayout;
            b.newLayout = newLayout;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = dst.img;
            b.subresourceRange = {aspect, baseMip, levelCount, 0, 1};

            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;
            vkCmdPipelineBarrier2(cmd, &dep);
        };

        // Level 0: UNDEFINED -> TRANSFER_DST, then the buffer -> image copy.
        barrier(0, 1, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copy{};
        copy.bufferOffset = 0;
        copy.bufferRowLength = 0;
        copy.bufferImageHeight = 0;
        copy.imageSubresource = {aspect, 0, 0, 1};
        copy.imageOffset = {0, 0, 0};
        copy.imageExtent = {dst.width, dst.height, 1};
        vkCmdCopyBufferToImage(cmd, staging.buf, dst.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &copy);

        if (mipLevels <= 1) {
            // Exact pre-T7 behavior: one image, one mip, straight to
            // SHADER_READ_ONLY_OPTIMAL. No blit chain to generate.
            barrier(0, 1, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            return;
        }

        // T7 mip remedy: standard box-downsample blit chain. Level 0 first
        // becomes a blit SOURCE (it was just written as a blit DST above);
        // every later level is blitted from the level directly above it,
        // halving width/height (floored, clamped to a 1x1 minimum), then
        // itself becomes a blit source for the next iteration.
        barrier(0, 1, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        int32_t mipW = static_cast<int32_t>(dst.width);
        int32_t mipH = static_cast<int32_t>(dst.height);
        for (uint32_t level = 1; level < mipLevels; ++level) {
            const int32_t nextW = mipW > 1 ? mipW / 2 : 1;
            const int32_t nextH = mipH > 1 ? mipH / 2 : 1;

            barrier(level, 1, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageBlit blit{};
            blit.srcSubresource = {aspect, level - 1, 0, 1};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mipW, mipH, 1};
            blit.dstSubresource = {aspect, level, 0, 1};
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {nextW, nextH, 1};
            vkCmdBlitImage(cmd, dst.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.img,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            barrier(level, 1, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            mipW = nextW;
            mipH = nextH;
        }

        // Every level is now TRANSFER_SRC_OPTIMAL (level 0 from the first
        // barrier above, every other level from its own loop iteration) --
        // one final barrier moves the whole chain to SHADER_READ_ONLY.
        barrier(0, mipLevels, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }, err);

    Destroy(ctx, staging);
    return ok;
}

void Resources::Destroy(VkContext& ctx, Buffer& buffer) {
    if (buffer.buf != VK_NULL_HANDLE) {
        vmaDestroyBuffer(ctx.Allocator(), buffer.buf, buffer.alloc);
    }
    buffer = Buffer{};
}

void Resources::Destroy(VkContext& ctx, Image2D& image) {
    if (image.view != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx.Device(), image.view, nullptr);
    }
    if (image.img != VK_NULL_HANDLE) {
        vmaDestroyImage(ctx.Allocator(), image.img, image.alloc);
    }
    image = Image2D{};
}

bool Resources::OneShot(VkContext& ctx, const std::function<void(VkCommandBuffer)>& record,
                        std::string& err) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = ctx.GraphicsFamily();

    VkCommandPool pool = VK_NULL_HANDLE;
    VkResult vr = vkCreateCommandPool(ctx.Device(), &poolInfo, nullptr, &pool);
    if (vr != VK_SUCCESS) {
        err = "vkCreateCommandPool (OneShot) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        return false;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = pool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vr = vkAllocateCommandBuffers(ctx.Device(), &cmdAllocInfo, &cmd);
    if (vr != VK_SUCCESS) {
        err = "vkAllocateCommandBuffers (OneShot) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        vkDestroyCommandPool(ctx.Device(), pool, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vr = vkBeginCommandBuffer(cmd, &beginInfo);
    if (vr != VK_SUCCESS) {
        err = "vkBeginCommandBuffer (OneShot) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        vkDestroyCommandPool(ctx.Device(), pool, nullptr);
        return false;
    }

    if (record) record(cmd);

    vr = vkEndCommandBuffer(cmd);
    if (vr != VK_SUCCESS) {
        err = "vkEndCommandBuffer (OneShot) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        vkDestroyCommandPool(ctx.Device(), pool, nullptr);
        return false;
    }

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = cmd;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdSubmitInfo;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    vr = vkCreateFence(ctx.Device(), &fenceInfo, nullptr, &fence);
    if (vr != VK_SUCCESS) {
        err = "vkCreateFence (OneShot) failed (VkResult " + std::to_string(static_cast<int>(vr)) +
              ")";
        vkDestroyCommandPool(ctx.Device(), pool, nullptr);
        return false;
    }

    vr = vkQueueSubmit2(ctx.GraphicsQueue(), 1, &submit, fence);
    if (vr != VK_SUCCESS) {
        err = "vkQueueSubmit2 (OneShot) failed (VkResult " + std::to_string(static_cast<int>(vr)) +
              ")";
        vkDestroyFence(ctx.Device(), fence, nullptr);
        vkDestroyCommandPool(ctx.Device(), pool, nullptr);
        return false;
    }

    vr = vkWaitForFences(ctx.Device(), 1, &fence, VK_TRUE, UINT64_MAX);
    if (vr != VK_SUCCESS) {
        err = "vkWaitForFences (OneShot) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        vkDestroyFence(ctx.Device(), fence, nullptr);
        vkDestroyCommandPool(ctx.Device(), pool, nullptr);
        return false;
    }

    vkDestroyFence(ctx.Device(), fence, nullptr);
    // Destroying the (TRANSIENT) pool also frees the command buffer
    // allocated from it -- no separate vkFreeCommandBuffers call needed.
    vkDestroyCommandPool(ctx.Device(), pool, nullptr);
    return true;
}

} // namespace Onyx::Rendering
