#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <volk.h>

#include "Onyx/Core/Result.h"
#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/FrameRing.h"
#include "Onyx/Gpu/Handles.h"
#include "Onyx/Gpu/Swapchain.h"

// Error codes: Gpu codes pass through (gpu.surface, gpu.extent, gpu.present, gpu.vk).
namespace Onyx::Shell {

// A swapchain with the semaphores the frame loop needs: one acquire semaphore per frame slot, one
// present semaphore per swapchain image. Acquire adds the acquire wait and the present signal to the
// frame, so FrameRing::End submits both. Drive it with one FrameRing; the surface belongs to the
// caller and outlives the presenter. Not thread-safe.
class Presenter {
public:
    static Result<std::unique_ptr<Presenter>> Create(Gpu::Device& device, VkSurfaceKHR surface, glm::ivec2 size);

    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;
    ~Presenter();

    // nullopt when the swapchain is out of date or flagged for recreation; the caller calls Resize and
    // skips the frame. Nothing is added to the frame in that case.
    Result<std::optional<Gpu::SwapImage>> Acquire(Gpu::Frame& frame);

    // Transitions the image to a colour attachment, clears it and begins dynamic rendering over the
    // whole swapchain extent with viewport and scissor set.
    void BeginRendering(VkCommandBuffer cmd, const Gpu::SwapImage& image, const glm::vec4& clear);
    // Ends rendering and transitions the image for presentation.
    void EndRendering(VkCommandBuffer cmd, const Gpu::SwapImage& image);

    // `submitted` is the value FrameRing::End returned for the frame that rendered the image; the
    // present waits on the semaphore that submit signals. An out-of-date or suboptimal result is not
    // an error: it sets NeedsRecreate.
    Result<void> Present(Gpu::FenceValue submitted, const Gpu::SwapImage& image);

    // Waits for the queue to go idle; never call it on a frame path that can avoid it.
    Result<void> Resize(glm::ivec2 size);

    bool NeedsRecreate() const { return m_swapchain->NeedsRecreate(); }
    VkFormat Format() const { return m_swapchain->Format(); }
    VkExtent2D Extent() const { return m_swapchain->Extent(); }
    uint32_t ImageCount() const { return m_swapchain->ImageCount(); }

private:
    explicit Presenter(Gpu::Device& device) : m_device(device) {}

    Result<void> SyncPresentSemaphores();
    Result<VkSemaphore> AcquireSemaphore(uint32_t slot);

    Gpu::Device& m_device;
    std::unique_ptr<Gpu::Swapchain> m_swapchain;
    std::vector<Gpu::Unique<VkSemaphore>> m_acquire;
    std::vector<Gpu::Unique<VkSemaphore>> m_present;
};

}
