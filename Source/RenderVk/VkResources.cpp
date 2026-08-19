#include <Onyx/RenderVk/VkResources.h>

#include <cstring>

namespace Onyx::RenderVk {

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
                                  std::string& err) {
    Image2D out{};
    if (width == 0 || height == 0) {
        err = "Resources::CreateImage2D: width/height must be nonzero";
        return out;
    }

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = format;
    imgInfo.extent = {width, height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = samples;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = usage;
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
    viewInfo.subresourceRange.levelCount = 1;
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

    bool ok = OneShot(ctx, [&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier2 toDst{};
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        toDst.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        toDst.srcAccessMask = VK_ACCESS_2_NONE;
        toDst.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toDst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = dst.img;
        toDst.subresourceRange = {aspect, 0, 1, 0, 1};

        VkDependencyInfo dep1{};
        dep1.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep1.imageMemoryBarrierCount = 1;
        dep1.pImageMemoryBarriers = &toDst;
        vkCmdPipelineBarrier2(cmd, &dep1);

        VkBufferImageCopy copy{};
        copy.bufferOffset = 0;
        copy.bufferRowLength = 0;
        copy.bufferImageHeight = 0;
        copy.imageSubresource = {aspect, 0, 0, 1};
        copy.imageOffset = {0, 0, 0};
        copy.imageExtent = {dst.width, dst.height, 1};
        vkCmdCopyBufferToImage(cmd, staging.buf, dst.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &copy);

        VkImageMemoryBarrier2 toRead{};
        toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        toRead.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toRead.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        toRead.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.image = dst.img;
        toRead.subresourceRange = {aspect, 0, 1, 0, 1};

        VkDependencyInfo dep2{};
        dep2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep2.imageMemoryBarrierCount = 1;
        dep2.pImageMemoryBarriers = &toRead;
        vkCmdPipelineBarrier2(cmd, &dep2);
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

} // namespace Onyx::RenderVk
