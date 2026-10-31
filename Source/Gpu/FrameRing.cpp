#include "Onyx/Gpu/FrameRing.h"

#include <atomic>
#include <format>

#include "VkError.h"

namespace Onyx::Gpu {

namespace {
std::atomic<uint64_t> g_nextRingId{1};
}

Result<Transient> Frame::Allocate(VkDeviceSize size, VkDeviceSize alignment) {
    if (!m_isOpen) {
        return Result<Transient>::Fail({"gpu.frame", "Allocate was called on a frame that is not open", 0});
    }
    if (alignment == 0) {
        alignment = 1;
    }
    const VkDeviceSize aligned = (m_offset + alignment - 1) / alignment * alignment;
    if (size > m_transient.size || aligned > m_transient.size - size) {
        return Result<Transient>::Fail(
            {"gpu.transientFull",
             std::format("transient ring of {} bytes cannot hold {} more bytes at offset {}", m_transient.size, size,
                         aligned),
             0});
    }
    m_offset = aligned + size;
    Transient out;
    out.buffer = m_transient.handle.Get();
    out.offset = aligned;
    out.size = size;
    out.mapped = static_cast<std::byte*>(m_transient.mapped) + aligned;
    return Result<Transient>::Ok(out);
}

void Frame::AddWait(FenceValue value) {
    if (value == 0) {
        return;
    }
    SemaphoreWait wait;
    wait.semaphore = m_device->GraphicsQueue().Timeline();
    wait.value = value;
    wait.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    m_waits.push_back(wait);
}

void Frame::AddWait(VkSemaphore timeline, uint64_t value, VkPipelineStageFlags2 stage) {
    SemaphoreWait wait;
    wait.semaphore = timeline;
    wait.value = value;
    wait.stage = stage;
    m_waits.push_back(wait);
}

void Frame::AddWait(VkSemaphore binary, VkPipelineStageFlags2 stage) {
    SemaphoreWait wait;
    wait.semaphore = binary;
    wait.value = 0;
    wait.stage = stage;
    m_waits.push_back(wait);
}

void Frame::AddSignal(VkSemaphore binary) {
    SemaphoreSignal signal;
    signal.semaphore = binary;
    signal.value = 0;
    signal.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    m_signals.push_back(signal);
}

Result<std::unique_ptr<FrameRing>> FrameRing::Create(Device& device, const FrameRingOptions& options) {
    if (options.framesInFlight == 0 || options.transientBytes == 0) {
        return Result<std::unique_ptr<FrameRing>>::Fail(
            {"gpu.frame", "framesInFlight and transientBytes must be greater than zero", 0});
    }
    std::unique_ptr<FrameRing> ring(new FrameRing(device, g_nextRingId.fetch_add(1)));
    const auto& fn = device.Fn();
    for (uint32_t i = 0; i < options.framesInFlight; ++i) {
        std::unique_ptr<Frame> frame(new Frame(device, i, ring->m_id));

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = device.GraphicsQueue().Family();
        VkCommandPool pool = VK_NULL_HANDLE;
        VkResult r = fn.vkCreateCommandPool(device.Handle(), &poolInfo, nullptr, &pool);
        if (r != VK_SUCCESS) {
            return Result<std::unique_ptr<FrameRing>>::Fail(VkFail("gpu.vk", "vkCreateCommandPool", r));
        }
        frame->m_pool = Unique<VkCommandPool>(&device, pool);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = pool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        r = fn.vkAllocateCommandBuffers(device.Handle(), &allocInfo, &frame->m_cmd);
        if (r != VK_SUCCESS) {
            return Result<std::unique_ptr<FrameRing>>::Fail(VkFail("gpu.vk", "vkAllocateCommandBuffers", r));
        }

        constexpr VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        auto transient = CreateBuffer(device, options.transientBytes, usage, MemoryUsage::HostToDevice);
        if (!transient.ok()) {
            return Result<std::unique_ptr<FrameRing>>::Fail(transient.error());
        }
        if (transient.value().mapped == nullptr) {
            return Result<std::unique_ptr<FrameRing>>::Fail(
                {"gpu.alloc", "the transient buffer is not host mapped", 0});
        }
        frame->m_transient = std::move(transient.value());
        ring->m_frames.push_back(std::move(frame));
    }
    return Result<std::unique_ptr<FrameRing>>::Ok(std::move(ring));
}

Result<Frame*> FrameRing::Begin() {
    if (m_open != nullptr) {
        return Result<Frame*>::Fail({"gpu.frame", "a frame is still open; call End before Begin", 0});
    }
    Frame& frame = *m_frames[m_cursor % m_frames.size()];
    Queue& queue = m_device.GraphicsQueue();
    if (frame.m_lastValue != 0) {
        auto waited = queue.Wait(frame.m_lastValue);
        if (!waited.ok()) {
            return Result<Frame*>::Fail(waited.error());
        }
    }
    m_device.Retire().Collect(queue.Completed());

    const auto& fn = m_device.Fn();
    VkResult r = fn.vkResetCommandPool(m_device.Handle(), frame.m_pool.Get(), 0);
    if (r != VK_SUCCESS) {
        return Result<Frame*>::Fail(VkFail("gpu.vk", "vkResetCommandPool", r));
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r = fn.vkBeginCommandBuffer(frame.m_cmd, &begin);
    if (r != VK_SUCCESS) {
        return Result<Frame*>::Fail(VkFail("gpu.vk", "vkBeginCommandBuffer", r));
    }
    frame.m_offset = 0;
    frame.m_waits.clear();
    frame.m_signals.clear();
    ++m_cursor;
    frame.m_isOpen = true;
    m_open = &frame;
    return Result<Frame*>::Ok(&frame);
}

Result<FenceValue> FrameRing::End(Frame& frame) {
    if (m_open != &frame) {
        return Result<FenceValue>::Fail({"gpu.frame", "End was called on a frame that is not the open one", 0});
    }
    m_open = nullptr;
    frame.m_isOpen = false;
    const auto& fn = m_device.Fn();
    VkResult r = fn.vkEndCommandBuffer(frame.m_cmd);
    if (r != VK_SUCCESS) {
        return Result<FenceValue>::Fail(VkFail("gpu.vk", "vkEndCommandBuffer", r));
    }
    // Host writes into a non-coherent allocation need an explicit flush; this is a no-op on coherent memory.
    r = vmaFlushAllocation(m_device.Allocator(), frame.m_transient.allocation, 0, VK_WHOLE_SIZE);
    if (r != VK_SUCCESS) {
        return Result<FenceValue>::Fail(VkFail("gpu.vk", "vmaFlushAllocation", r));
    }

    const VkCommandBuffer cmd = frame.m_cmd;
    SubmitDesc desc;
    desc.commandBuffers = std::span<const VkCommandBuffer>(&cmd, 1);
    desc.waits = frame.m_waits;
    desc.signals = frame.m_signals;
    auto value = m_device.GraphicsQueue().Submit(desc);
    if (!value.ok()) {
        return value;
    }
    frame.m_lastValue = value.value();
    m_lastValue = value.value();
    return value;
}

}
