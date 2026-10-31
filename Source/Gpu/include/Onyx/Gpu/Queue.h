#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>

#include <volk.h>

#include "Onyx/Core/Result.h"
#include "Onyx/Gpu/Retire.h"

namespace Onyx::Gpu {

class Device;

struct SemaphoreWait {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    uint64_t value = 0;
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
};

struct SemaphoreSignal {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    uint64_t value = 0;
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
};

struct SubmitDesc {
    std::span<const VkCommandBuffer> commandBuffers;
    std::span<const SemaphoreWait> waits;
    std::span<const SemaphoreSignal> signals;
};

class Queue {
public:
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    ~Queue();

    Result<FenceValue> Submit(const SubmitDesc& desc);
    Result<VkResult> Present(const VkPresentInfoKHR& info);

    FenceValue LastSubmitted() const;
    FenceValue Completed() const;
    Result<void> Wait(FenceValue value, uint64_t timeoutNs = UINT64_MAX);

    VkSemaphore Timeline() const { return m_timeline; }
    VkQueue Handle() const { return m_queue; }
    uint32_t Family() const { return m_family; }

private:
    friend class Device;
    Queue(Device& device, VkQueue queue, uint32_t family);
    Result<void> Init();

    Device& m_device;
    VkQueue m_queue;
    uint32_t m_family;
    VkSemaphore m_timeline = VK_NULL_HANDLE;
    std::mutex m_mutex;
    std::atomic<FenceValue> m_next{0};
};

}
