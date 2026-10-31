#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <volk.h>

#include "Onyx/Core/Domain/Texture.h"
#include "Onyx/Core/Result.h"
#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/Handles.h"
#include "Onyx/Gpu/Queue.h"

namespace Onyx::Gpu {

struct UploadOptions {
    VkDeviceSize stagingBytes = 64u << 20;
};

// Batches uploads into one command buffer and one Queue::Submit per Flush.
//
// Resources returned by Upload are not usable on the GPU until the value returned by Flush (or
// LastFlushed) has been waited on, either through Frame::AddWait or Queue::Wait; they must not be
// destroyed before Flush has submitted the copies that fill them. Staging is a single host-visible
// ring: when a request does not fit in the remaining space the context flushes, waits for that value
// and restarts the ring, which is the only case where Flush runs more than once per load. A request
// larger than the whole ring gets a dedicated staging buffer that lives until its flush completes.
//
// Error codes: gpu.upload.size (empty or short data, copy range out of bounds), gpu.upload.format
// (the device cannot sample the texture's format), plus gpu.alloc, gpu.submit, gpu.wait and gpu.vk.
// Must be destroyed before its Device. Not thread-safe.
class UploadContext {
public:
    static Result<std::unique_ptr<UploadContext>> Create(Device& device, const UploadOptions& options = {});

    UploadContext(const UploadContext&) = delete;
    UploadContext& operator=(const UploadContext&) = delete;
    ~UploadContext();

    // Device-local buffer; usage gains TRANSFER_DST and TRANSFER_SRC. Records a copy, does not submit.
    Result<Buffer> Upload(std::span<const std::byte> bytes, VkBufferUsageFlags usage);
    // Rgba8 becomes R8G8B8A8_UNORM with a full mip chain built by blits; Bc1/3/4/5/7 become the BC
    // formats with one mip; RgbaF16 becomes R16G16B16A16_SFLOAT with one mip. Ends in
    // SHADER_READ_ONLY_OPTIMAL.
    Result<Image> Upload(const Domain::Texture& texture);

    // One Queue::Submit if anything is pending, otherwise the last flushed value. A failure (the
    // vkEndCommandBuffer error, or the Submit error unchanged) drops the pending batch and is returned;
    // LastFlushed() keeps its previous value.
    Result<FenceValue> Flush();
    // Synchronous: flushes, copies `size` bytes of the buffer (which needs TRANSFER_SRC, as every
    // buffer from Upload has) into a host buffer, submits and waits. For tests and headless tools.
    Result<std::vector<std::byte>> Download(const Buffer& buffer, VkDeviceSize size);
    FenceValue LastFlushed() const { return m_lastFlushed; }

private:
    struct Staged {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        void* mapped = nullptr;
        VmaAllocation allocation = VK_NULL_HANDLE;
    };
    struct InFlight {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        FenceValue value = 0;
        std::vector<Buffer> staging;
    };

    explicit UploadContext(Device& device) : m_device(device) {}

    Result<VkCommandBuffer> Recording();
    Result<Staged> Stage(VkDeviceSize size);
    void Reap();

    Device& m_device;
    Unique<VkCommandPool> m_pool;
    Buffer m_staging;
    VkDeviceSize m_stagingOffset = 0;
    VkDeviceSize m_copyAlignment = 256;
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
    std::vector<Buffer> m_oneOff;
    std::vector<InFlight> m_inFlight;
    FenceValue m_lastFlushed = 0;
};

}
