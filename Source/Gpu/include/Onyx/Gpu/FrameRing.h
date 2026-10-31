#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <volk.h>

#include "Onyx/Core/Result.h"
#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/Handles.h"
#include "Onyx/Gpu/Queue.h"

namespace Onyx::Gpu {

struct Transient {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};

// One slot of the ring. Owned by FrameRing; valid from FrameRing::Begin until the next Begin that
// returns the same slot.
class Frame {
public:
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

    VkCommandBuffer Cmd() const { return m_cmd; }
    uint32_t Index() const { return m_index; }

    // Bump allocation from this frame's host-visible buffer (usage UNIFORM | STORAGE | VERTEX | INDEX |
    // TRANSFER_SRC). The caller owns any alignment the use needs beyond `alignment`. A zero alignment
    // means one. Fails with gpu.transientFull when the remaining space cannot hold the request.
    Result<Transient> Allocate(VkDeviceSize size, VkDeviceSize alignment);

    // Timeline wait on the device queue before this frame's commands run.
    void AddWait(FenceValue value);
    // Binary semaphore wait, for example the swapchain acquire semaphore.
    void AddWait(VkSemaphore binary, VkPipelineStageFlags2 stage);
    // Binary semaphore signalled when this frame's commands finish, for example the present semaphore.
    void AddSignal(VkSemaphore binary);

private:
    friend class FrameRing;
    Frame(Device& device, uint32_t index) : m_device(&device), m_index(index) {}

    Device* m_device;
    uint32_t m_index;
    Unique<VkCommandPool> m_pool;
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
    Buffer m_transient;
    VkDeviceSize m_offset = 0;
    FenceValue m_lastValue = 0;
    std::vector<SemaphoreWait> m_waits;
    std::vector<SemaphoreSignal> m_signals;
};

struct FrameRingOptions {
    uint32_t framesInFlight = 2;
    VkDeviceSize transientBytes = 8u << 20;
};

// Must be destroyed before its Device. Not thread-safe: one thread drives Begin and End.
class FrameRing {
public:
    static Result<std::unique_ptr<FrameRing>> Create(Device& device, const FrameRingOptions& options = {});

    FrameRing(const FrameRing&) = delete;
    FrameRing& operator=(const FrameRing&) = delete;

    // Waits for the next slot's previous submit, collects retired resources, resets the slot's pool and
    // transient buffer, and begins its command buffer. Fails with gpu.frame while a frame is still open.
    Result<Frame*> Begin();
    // Ends the command buffer and submits it with the frame's waits and signals; the returned value is
    // recorded on the slot and is what the slot's next Begin waits for.
    Result<FenceValue> End(Frame& frame);

    uint32_t FramesInFlight() const { return static_cast<uint32_t>(m_frames.size()); }
    FenceValue LastValue() const { return m_lastValue; }

private:
    explicit FrameRing(Device& device) : m_device(device) {}

    Device& m_device;
    std::vector<std::unique_ptr<Frame>> m_frames;
    uint64_t m_cursor = 0;
    Frame* m_open = nullptr;
    FenceValue m_lastValue = 0;
};

}
