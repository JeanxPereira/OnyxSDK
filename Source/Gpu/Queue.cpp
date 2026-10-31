#include "Onyx/Gpu/Queue.h"

#include <format>
#include <vector>

#include "Onyx/Gpu/Device.h"
#include "VkError.h"

namespace Onyx::Gpu {

namespace {

VkSemaphoreSubmitInfo SemaphoreInfo(VkSemaphore semaphore, uint64_t value, VkPipelineStageFlags2 stage) {
    VkSemaphoreSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    info.semaphore = semaphore;
    info.value = value;
    info.stageMask = stage;
    return info;
}

}

Queue::Queue(Device& device, VkQueue queue, uint32_t family)
    : m_device(device), m_queue(queue), m_family(family) {}

Queue::~Queue() {
    if (m_timeline != VK_NULL_HANDLE) {
        m_device.Fn().vkDestroySemaphore(m_device.Handle(), m_timeline, nullptr);
    }
}

Result<void> Queue::Init() {
    VkSemaphoreTypeCreateInfo type{};
    type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type.initialValue = 0;
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = &type;
    VkResult r = m_device.Fn().vkCreateSemaphore(m_device.Handle(), &info, nullptr, &m_timeline);
    if (r != VK_SUCCESS) {
        m_timeline = VK_NULL_HANDLE;
        return Result<void>::Fail(VkFail("gpu.timeline", "vkCreateSemaphore", r));
    }
    return Result<void>::Ok();
}

Result<FenceValue> Queue::Submit(const SubmitDesc& desc) {
    std::vector<VkCommandBufferSubmitInfo> buffers;
    buffers.reserve(desc.commandBuffers.size());
    for (VkCommandBuffer cb : desc.commandBuffers) {
        VkCommandBufferSubmitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        info.commandBuffer = cb;
        buffers.push_back(info);
    }
    std::vector<VkSemaphoreSubmitInfo> waits;
    waits.reserve(desc.waits.size());
    for (const SemaphoreWait& w : desc.waits) {
        waits.push_back(SemaphoreInfo(w.semaphore, w.value, w.stage));
    }
    std::vector<VkSemaphoreSubmitInfo> signals;
    signals.reserve(desc.signals.size() + 1);
    for (const SemaphoreSignal& s : desc.signals) {
        signals.push_back(SemaphoreInfo(s.semaphore, s.value, s.stage));
    }

    std::lock_guard lock(m_mutex);
    const FenceValue value = m_next.load() + 1;
    signals.push_back(SemaphoreInfo(m_timeline, value, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size());
    submit.pWaitSemaphoreInfos = waits.data();
    submit.commandBufferInfoCount = static_cast<uint32_t>(buffers.size());
    submit.pCommandBufferInfos = buffers.data();
    submit.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
    submit.pSignalSemaphoreInfos = signals.data();

    VkResult r = m_device.Fn().vkQueueSubmit2(m_queue, 1, &submit, VK_NULL_HANDLE);
    if (r != VK_SUCCESS) {
        Error error = VkFail("gpu.submit", "vkQueueSubmit2", r);
        SetFault(error);
        return Result<FenceValue>::Fail(std::move(error));
    }
    m_next.store(value);
    m_device.CountSubmit();
    return Result<FenceValue>::Ok(value);
}

Result<VkResult> Queue::Present(const VkPresentInfoKHR& info) {
    if (m_device.Fn().vkQueuePresentKHR == nullptr) {
        Error error{"gpu.present", "VK_KHR_swapchain is not enabled on this device", 0};
        SetFault(error);
        return Result<VkResult>::Fail(std::move(error));
    }
    std::lock_guard lock(m_mutex);
    return Result<VkResult>::Ok(m_device.Fn().vkQueuePresentKHR(m_queue, &info));
}

FenceValue Queue::LastSubmitted() const {
    return m_next.load();
}

FenceValue Queue::Completed() const {
    uint64_t value = 0;
    VkResult r = m_device.Fn().vkGetSemaphoreCounterValue(m_device.Handle(), m_timeline, &value);
    if (r != VK_SUCCESS) {
        SetFault(VkFail("gpu.vk", "vkGetSemaphoreCounterValue", r));
        return m_lastCompleted.load();
    }
    FenceValue seen = m_lastCompleted.load();
    while (seen < value && !m_lastCompleted.compare_exchange_weak(seen, value)) {
    }
    return value;
}

std::optional<Error> Queue::Fault() const {
    std::lock_guard lock(m_faultMutex);
    return m_fault;
}

void Queue::SetFault(Error error) const {
    std::lock_guard lock(m_faultMutex);
    m_fault = std::move(error);
}

Result<void> Queue::Wait(FenceValue value, uint64_t timeoutNs) {
    VkSemaphoreWaitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    info.semaphoreCount = 1;
    info.pSemaphores = &m_timeline;
    info.pValues = &value;
    VkResult r = m_device.Fn().vkWaitSemaphores(m_device.Handle(), &info, timeoutNs);
    if (r == VK_SUCCESS) {
        return Result<void>::Ok();
    }
    Error error = VkFail(r == VK_TIMEOUT ? "gpu.timeout" : "gpu.wait", "vkWaitSemaphores", r);
    if (r != VK_TIMEOUT) {
        SetFault(error);
    }
    return Result<void>::Fail(std::move(error));
}

}
