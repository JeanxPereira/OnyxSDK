#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
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
    // On a vkGetSemaphoreCounterValue error this returns the last value read successfully
    // and records the failure in Fault().
    FenceValue Completed() const;
    Result<void> Wait(FenceValue value, uint64_t timeoutNs = UINT64_MAX);
    // vkQueueWaitIdle under the queue mutex. Swapchain teardown and recreate only; never on a frame path.
    Result<void> WaitIdle();

    // Holds the mutex Submit, Present and WaitIdle take, for code that must touch Handle() directly (a
    // third-party renderer that submits and waits on the queue itself). Calling Submit, Present or
    // WaitIdle on this queue while holding it deadlocks.
    [[nodiscard]] std::unique_lock<std::mutex> Lock() { return std::unique_lock<std::mutex>(m_mutex); }

    // Sticky: the most recent failure from Submit, Wait (a timeout is not a fault), Present
    // (missing swapchain entry point) or Completed. Never cleared.
    std::optional<Error> Fault() const;

    VkSemaphore Timeline() const { return m_timeline; }
    VkQueue Handle() const { return m_queue; }
    uint32_t Family() const { return m_family; }

private:
    friend class Device;
    Queue(Device& device, VkQueue queue, uint32_t family);
    Result<void> Init();
    void SetFault(Error error) const;

    Device& m_device;
    VkQueue m_queue;
    uint32_t m_family;
    VkSemaphore m_timeline = VK_NULL_HANDLE;
    std::mutex m_mutex;
    std::atomic<FenceValue> m_next{0};
    mutable std::atomic<FenceValue> m_lastCompleted{0};
    mutable std::mutex m_faultMutex;
    mutable std::optional<Error> m_fault;
};

}
