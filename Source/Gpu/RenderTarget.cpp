#include "Onyx/Gpu/RenderTarget.h"

#include <array>
#include <cstring>
#include <span>
#include <utility>

#include "Onyx/Gpu/Device.h"
#include "VkError.h"

namespace Onyx::Gpu {

namespace {

constexpr VkPipelineStageFlags2 kSampleStages =
    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
constexpr VkAccessFlags2 kSampleAccess = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;

bool HasStencil(VkFormat format) {
    return format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT ||
           format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_S8_UINT;
}

struct Transition {
    VkImage image = VK_NULL_HANDLE;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageLayout from = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout to = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 srcStage = 0;
    VkAccessFlags2 srcAccess = 0;
    VkPipelineStageFlags2 dstStage = 0;
    VkAccessFlags2 dstAccess = 0;
};

void Barrier(Device& device, VkCommandBuffer cmd, std::span<const Transition> transitions) {
    std::array<VkImageMemoryBarrier2, 3> barriers{};
    uint32_t count = 0;
    for (const Transition& t : transitions) {
        VkImageMemoryBarrier2& b = barriers[count++];
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask = t.srcStage;
        b.srcAccessMask = t.srcAccess;
        b.dstStageMask = t.dstStage;
        b.dstAccessMask = t.dstAccess;
        b.oldLayout = t.from;
        b.newLayout = t.to;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = t.image;
        b.subresourceRange = {t.aspect, 0, 1, 0, 1};
    }
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = count;
    dep.pImageMemoryBarriers = barriers.data();
    device.Fn().vkCmdPipelineBarrier2(cmd, &dep);
}

bool IsBgra8(VkFormat format) {
    return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

bool IsRgba8(VkFormat format) {
    return format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB;
}

}

Result<std::unique_ptr<RenderTarget>> RenderTarget::Create(Device& device, const RenderTargetDesc& desc) {
    if (desc.extent.width == 0 || desc.extent.height == 0) {
        return Result<std::unique_ptr<RenderTarget>>::Fail({"gpu.extent", "render target extent must be non-zero", 0});
    }
    std::unique_ptr<RenderTarget> target(new RenderTarget(device, desc));
    auto created = target->CreateImages(desc.extent);
    if (!created.ok()) {
        return Result<std::unique_ptr<RenderTarget>>::Fail(created.error());
    }
    return Result<std::unique_ptr<RenderTarget>>::Ok(std::move(target));
}

Result<void> RenderTarget::CreateImages(VkExtent2D extent) {
    const bool multisampled = m_desc.samples != VK_SAMPLE_COUNT_1_BIT;

    ImageDesc resolveDesc;
    resolveDesc.extent = extent;
    resolveDesc.format = m_desc.colorFormat;
    resolveDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto resolve = CreateImage(m_device, resolveDesc);
    if (!resolve.ok()) {
        return Result<void>::Fail(resolve.error());
    }

    Image color;
    if (multisampled) {
        ImageDesc colorDesc;
        colorDesc.extent = extent;
        colorDesc.format = m_desc.colorFormat;
        colorDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        colorDesc.samples = m_desc.samples;
        auto created = CreateImage(m_device, colorDesc);
        if (!created.ok()) {
            return Result<void>::Fail(created.error());
        }
        color = std::move(created.value());
    }

    Image depth;
    if (m_desc.depthFormat != VK_FORMAT_UNDEFINED) {
        ImageDesc depthDesc;
        depthDesc.extent = extent;
        depthDesc.format = m_desc.depthFormat;
        depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthDesc.samples = m_desc.samples;
        depthDesc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (HasStencil(m_desc.depthFormat)) {
            depthDesc.aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        auto created = CreateImage(m_device, depthDesc);
        if (!created.ok()) {
            return Result<void>::Fail(created.error());
        }
        depth = std::move(created.value());
    }

    // Assigning retires the previous images through Unique at the last submitted fence value.
    m_resolve = std::move(resolve.value());
    m_color = std::move(color);
    m_depth = std::move(depth);
    m_desc.extent = extent;
    return Result<void>::Ok();
}

Result<void> RenderTarget::Resize(VkExtent2D extent) {
    if (extent.width == 0 || extent.height == 0) {
        return Result<void>::Fail({"gpu.extent", "render target extent must be non-zero", 0});
    }
    if (extent.width == m_desc.extent.width && extent.height == m_desc.extent.height) {
        return Result<void>::Ok();
    }
    auto created = CreateImages(extent);
    if (!created.ok()) {
        return created;
    }
    ++m_generation;
    return Result<void>::Ok();
}

void RenderTarget::Begin(VkCommandBuffer cmd, const glm::vec4& clear) {
    const auto& fn = m_device.Fn();
    const bool multisampled = m_color.handle.Get() != VK_NULL_HANDLE;
    const bool hasDepth = m_depth.handle.Get() != VK_NULL_HANDLE;

    constexpr VkPipelineStageFlags2 kAllStages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    constexpr VkAccessFlags2 kAllWrites = VK_ACCESS_2_MEMORY_WRITE_BIT;
    constexpr VkAccessFlags2 kColorAccess = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

    std::array<Transition, 3> transitions{};
    size_t count = 0;
    transitions[count++] = {m_resolve.handle.Get(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, kAllStages, kAllWrites,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, kColorAccess};
    if (multisampled) {
        transitions[count++] = {m_color.handle.Get(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, kAllStages, kAllWrites,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, kColorAccess};
    }
    VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (HasStencil(m_desc.depthFormat)) {
        depthAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    if (hasDepth) {
        transitions[count++] = {m_depth.handle.Get(), depthAspect, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, kAllStages, kAllWrites,
                                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    }
    Barrier(m_device, cmd, std::span<const Transition>(transitions.data(), count));

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.clearValue.color = {{clear.r, clear.g, clear.b, clear.a}};
    if (multisampled) {
        colorAttachment.imageView = m_color.view.Get();
        colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        colorAttachment.resolveImageView = m_resolve.view.Get();
        colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    } else {
        colorAttachment.imageView = m_resolve.view.Get();
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = m_depth.view.Get();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    const VkExtent2D extent = m_desc.extent;
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &colorAttachment;
    if (hasDepth) {
        rendering.pDepthAttachment = &depthAttachment;
        if (HasStencil(m_desc.depthFormat)) {
            rendering.pStencilAttachment = &depthAttachment;
        }
    }
    fn.vkCmdBeginRendering(cmd, &rendering);

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    fn.vkCmdSetViewport(cmd, 0, 1, &viewport);
    fn.vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void RenderTarget::End(VkCommandBuffer cmd) {
    m_device.Fn().vkCmdEndRendering(cmd);
    Transition toRead{m_resolve.handle.Get(),
                      VK_IMAGE_ASPECT_COLOR_BIT,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                      kSampleStages,
                      kSampleAccess};
    Barrier(m_device, cmd, std::span<const Transition>(&toRead, 1));
}

Result<std::vector<std::byte>> ReadbackColor(Device& device, RenderTarget& target, FenceValue after) {
    using Out = Result<std::vector<std::byte>>;
    const VkFormat format = target.Desc().colorFormat;
    if (!IsRgba8(format) && !IsBgra8(format)) {
        return Out::Fail({"gpu.vk", "ReadbackColor supports 8-bit RGBA and BGRA colour formats only", 0});
    }
    const VkExtent2D extent = target.Extent();
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
    const auto& fn = device.Fn();

    auto staging = CreateBuffer(device, byteCount, VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryUsage::DeviceToHost);
    if (!staging.ok()) {
        return Out::Fail(staging.error());
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = device.GraphicsQueue().Family();
    VkCommandPool rawPool = VK_NULL_HANDLE;
    VkResult r = fn.vkCreateCommandPool(device.Handle(), &poolInfo, nullptr, &rawPool);
    if (r != VK_SUCCESS) {
        return Out::Fail(VkFail("gpu.vk", "vkCreateCommandPool", r));
    }
    Unique<VkCommandPool> pool(&device, rawPool);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = rawPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    r = fn.vkAllocateCommandBuffers(device.Handle(), &allocInfo, &cmd);
    if (r != VK_SUCCESS) {
        return Out::Fail(VkFail("gpu.vk", "vkAllocateCommandBuffers", r));
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r = fn.vkBeginCommandBuffer(cmd, &begin);
    if (r != VK_SUCCESS) {
        return Out::Fail(VkFail("gpu.vk", "vkBeginCommandBuffer", r));
    }

    const VkImage image = target.ResolveImage();
    Transition toCopy{image,
                      VK_IMAGE_ASPECT_COLOR_BIT,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      kSampleStages,
                      0,
                      VK_PIPELINE_STAGE_2_COPY_BIT,
                      VK_ACCESS_2_TRANSFER_READ_BIT};
    Barrier(device, cmd, std::span<const Transition>(&toCopy, 1));

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    fn.vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.value().handle.Get(), 1, &region);

    Transition toRead{image,
                      VK_IMAGE_ASPECT_COLOR_BIT,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_2_COPY_BIT,
                      0,
                      kSampleStages,
                      kSampleAccess};
    Barrier(device, cmd, std::span<const Transition>(&toRead, 1));

    VkBufferMemoryBarrier2 hostBarrier{};
    hostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    hostBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    hostBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    hostBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.buffer = staging.value().handle.Get();
    hostBarrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers = &hostBarrier;
    fn.vkCmdPipelineBarrier2(cmd, &dep);

    r = fn.vkEndCommandBuffer(cmd);
    if (r != VK_SUCCESS) {
        return Out::Fail(VkFail("gpu.vk", "vkEndCommandBuffer", r));
    }

    Queue& queue = device.GraphicsQueue();
    std::array cmds{cmd};
    std::array<SemaphoreWait, 1> waits{SemaphoreWait{queue.Timeline(), after, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT}};
    SubmitDesc submit;
    submit.commandBuffers = cmds;
    if (after > 0) {
        submit.waits = waits;
    }
    auto submitted = queue.Submit(submit);
    if (!submitted.ok()) {
        return Out::Fail(submitted.error());
    }
    auto waited = queue.Wait(submitted.value());
    if (!waited.ok()) {
        return Out::Fail(waited.error());
    }

    r = vmaInvalidateAllocation(device.Allocator(), staging.value().allocation, 0, VK_WHOLE_SIZE);
    if (r != VK_SUCCESS) {
        return Out::Fail(VkFail("gpu.vk", "vmaInvalidateAllocation", r));
    }

    std::vector<std::byte> pixels(static_cast<size_t>(byteCount));
    std::memcpy(pixels.data(), staging.value().mapped, pixels.size());
    if (IsBgra8(format)) {
        for (size_t i = 0; i < pixels.size(); i += 4) {
            std::swap(pixels[i], pixels[i + 2]);
        }
    }
    return Out::Ok(std::move(pixels));
}

}
