#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
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
// destroyed before Flush has submitted the copies that fill them.
//
// Staging is one host-visible ring. Each Flush hands the bytes its batch staged to the queue, and they
// return to the ring once that submission completes. A request that does not fit in the free part of
// the ring waits for the oldest flushed batch still holding staging bytes (never for anything submitted
// after it) and tries again. Only when the batch recorded since the last Flush fills the ring by itself
// does the context flush that batch and wait for it, which is the only case where Flush runs more than
// once per load (FullDrains counts it). A request larger than the whole ring gets a dedicated staging
// buffer that lives until its flush completes, without waiting.
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
    // Records a copy of `texture` into `image`, which Upload(texture) returned for a texture of the same
    // extent and format: every level is rewritten and the image ends in SHADER_READ_ONLY_OPTIMAL again.
    // The copy waits for all earlier work on the queue that touches the image; a submission that samples
    // it after the copy sees the new texels. On the host it waits only as staging requires (see above).
    // gpu.upload.size when the extent, format or byte count differ.
    Result<void> UploadInto(const Domain::Texture& texture, const Image& image);

    // One Queue::Submit if anything is pending, otherwise the last flushed value. A failure (the
    // vkEndCommandBuffer error, or the Submit error unchanged) drops the pending batch and is returned;
    // LastFlushed() keeps its previous value.
    Result<FenceValue> Flush();
    // Synchronous: flushes, copies `size` bytes of the buffer (which needs TRANSFER_SRC, as every
    // buffer from Upload has) into a host buffer, submits and waits. For tests and headless tools.
    Result<std::vector<std::byte>> Download(const Buffer& buffer, VkDeviceSize size);
    FenceValue LastFlushed() const { return m_lastFlushed; }
    // Times staging flushed the pending batch and waited for it because the batch itself no longer fit.
    uint64_t FullDrains() const { return m_fullDrains; }

private:
    struct Staged {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        void* mapped = nullptr;
        VmaAllocation allocation = VK_NULL_HANDLE;
    };
    // A flushed batch's staging bytes end at `end` (a ring position) and return once `value` completes.
    struct StagingUse {
        FenceValue value = 0;
        uint64_t end = 0;
    };
    struct InFlight {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        FenceValue value = 0;
        std::vector<Buffer> staging;
    };

    explicit UploadContext(Device& device) : m_device(device) {}

    Result<VkCommandBuffer> Recording();
    Result<Staged> Stage(VkDeviceSize size);
    void ReclaimStaging(FenceValue completed);
    // Where the batch being recorded began staging.
    uint64_t PendingStagingBegin() const;
    Result<void> RecordTexture(const Domain::Texture& texture, VkImage image, uint32_t levels, bool overwrite);
    void Reap();

    Device& m_device;
    Unique<VkCommandPool> m_pool;
    Buffer m_staging;
    // Ring positions grow without bound; the byte offset is position % m_staging.size. Bytes in
    // [m_stagingTail, m_stagingHead) belong to flushed batches in flight and to the pending batch.
    uint64_t m_stagingHead = 0;
    uint64_t m_stagingTail = 0;
    std::deque<StagingUse> m_stagingInFlight;
    VkDeviceSize m_copyAlignment = 256;
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
    std::vector<Buffer> m_oneOff;
    std::vector<InFlight> m_inFlight;
    FenceValue m_lastFlushed = 0;
    uint64_t m_fullDrains = 0;
};

}
