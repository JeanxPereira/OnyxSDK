#include "Onyx/Shell/Presenter.h"

#include <array>
#include <span>
#include <string>
#include <utility>

namespace Onyx::Shell {

namespace {

Result<Gpu::Unique<VkSemaphore>> CreateBinarySemaphore(Gpu::Device& device) {
    using Out = Result<Gpu::Unique<VkSemaphore>>;
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    const VkResult result = device.Fn().vkCreateSemaphore(device.Handle(), &info, nullptr, &semaphore);
    if (result != VK_SUCCESS) {
        return Out::Fail({"gpu.vk", "vkCreateSemaphore failed (VkResult " + std::to_string(result) + ")",
                          static_cast<int32_t>(result)});
    }
    return Out::Ok(Gpu::Unique<VkSemaphore>(&device, semaphore));
}

void Transition(Gpu::Device& device, VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                VkAccessFlags2 dstAccess) {
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
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    device.Fn().vkCmdPipelineBarrier2(cmd, &dependency);
}

VkExtent2D ToExtent(glm::ivec2 size) {
    return {static_cast<uint32_t>(size.x > 0 ? size.x : 0), static_cast<uint32_t>(size.y > 0 ? size.y : 0)};
}

}

Result<std::unique_ptr<Presenter>> Presenter::Create(Gpu::Device& device, VkSurfaceKHR surface, glm::ivec2 size) {
    using Out = Result<std::unique_ptr<Presenter>>;
    std::unique_ptr<Presenter> presenter(new Presenter(device));
    Gpu::SwapchainOptions options;
    options.surface = surface;
    options.extent = ToExtent(size);
    auto swapchain = Gpu::Swapchain::Create(device, options);
    if (!swapchain.ok()) {
        return Out::Fail(swapchain.error());
    }
    presenter->m_swapchain = std::move(swapchain.value());
    auto synced = presenter->SyncPresentSemaphores();
    if (!synced.ok()) {
        return Out::Fail(synced.error());
    }
    return Out::Ok(std::move(presenter));
}

Presenter::~Presenter() {
    // The semaphores retire at the last submitted value; the swapchain waits for the queue itself.
    m_swapchain.reset();
}

Result<void> Presenter::SyncPresentSemaphores() {
    const size_t wanted = m_swapchain->ImageCount();
    if (m_present.size() == wanted) {
        return Result<void>::Ok();
    }
    m_present.clear();
    for (size_t i = 0; i < wanted; ++i) {
        auto semaphore = CreateBinarySemaphore(m_device);
        if (!semaphore.ok()) {
            return Result<void>::Fail(semaphore.error());
        }
        m_present.push_back(std::move(semaphore.value()));
    }
    return Result<void>::Ok();
}

Result<VkSemaphore> Presenter::AcquireSemaphore(uint32_t slot) {
    while (m_acquire.size() <= slot) {
        auto semaphore = CreateBinarySemaphore(m_device);
        if (!semaphore.ok()) {
            return Result<VkSemaphore>::Fail(semaphore.error());
        }
        m_acquire.push_back(std::move(semaphore.value()));
    }
    return Result<VkSemaphore>::Ok(m_acquire[slot].Get());
}

Result<std::optional<Gpu::SwapImage>> Presenter::Acquire(Gpu::Frame& frame) {
    using Out = Result<std::optional<Gpu::SwapImage>>;
    if (m_swapchain->NeedsRecreate()) {
        return Out::Ok(std::nullopt);
    }
    auto semaphore = AcquireSemaphore(frame.Index());
    if (!semaphore.ok()) {
        return Out::Fail(semaphore.error());
    }
    auto image = m_swapchain->Acquire(semaphore.value());
    if (!image.ok()) {
        return Out::Fail(image.error());
    }
    if (!image.value().has_value()) {
        return Out::Ok(std::nullopt);
    }
    frame.AddWait(semaphore.value(), VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    frame.AddSignal(m_present[image.value()->index].Get());
    return Out::Ok(image.value());
}

void Presenter::BeginRendering(VkCommandBuffer cmd, const Gpu::SwapImage& image, const glm::vec4& clear) {
    const auto& fn = m_device.Fn();
    // The source stage chains after the acquire semaphore's wait stage so the layout change cannot race
    // the presentation engine's last read of the image.
    Transition(m_device, cmd, image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = image.view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = {{clear.r, clear.g, clear.b, clear.a}};

    const VkExtent2D extent = m_swapchain->Extent();
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    fn.vkCmdBeginRendering(cmd, &rendering);

    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    fn.vkCmdSetViewport(cmd, 0, 1, &viewport);
    fn.vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void Presenter::EndRendering(VkCommandBuffer cmd, const Gpu::SwapImage& image) {
    m_device.Fn().vkCmdEndRendering(cmd);
    Transition(m_device, cmd, image.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
               VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE);
}

Result<void> Presenter::Present(Gpu::FenceValue, const Gpu::SwapImage& image) {
    const std::array<VkSemaphore, 1> wait{m_present[image.index].Get()};
    return m_swapchain->Present(wait, image.index);
}

Result<void> Presenter::Resize(glm::ivec2 size) {
    auto recreated = m_swapchain->Recreate(ToExtent(size));
    if (!recreated.ok()) {
        return recreated;
    }
    return SyncPresentSemaphores();
}

}
