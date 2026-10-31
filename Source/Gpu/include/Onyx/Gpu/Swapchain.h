#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <volk.h>

#include "Onyx/Core/Result.h"

namespace Onyx::Gpu {

class Device;

struct SwapchainOptions {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkExtent2D extent{};
    bool vsync = true;
    VkFormat preferredFormat = VK_FORMAT_B8G8R8A8_UNORM;
};

struct SwapImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t index = 0;
};

// The surface belongs to the host and must outlive the swapchain. Destruction waits for the last
// queue submission, then destroys the swapchain and its views.
class Swapchain {
public:
    // gpu.surface when the graphics queue family cannot present to the surface, gpu.present when the
    // device was created without presentSupport, gpu.extent when the surface has no drawable area.
    static Result<std::unique_ptr<Swapchain>> Create(Device& device, const SwapchainOptions& options);

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    ~Swapchain();

    // nullopt: the swapchain is out of date and the caller recreates it. A suboptimal swapchain
    // returns the image and sets NeedsRecreate().
    Result<std::optional<SwapImage>> Acquire(VkSemaphore signal);
    // Out of date and suboptimal results set NeedsRecreate() and are not errors.
    Result<void> Present(std::span<const VkSemaphore> wait, uint32_t index);
    // The one place besides ~Device that calls vkDeviceWaitIdle.
    Result<void> Recreate(VkExtent2D extent);

    bool NeedsRecreate() const { return m_needsRecreate; }
    VkFormat Format() const { return m_format; }
    VkExtent2D Extent() const { return m_extent; }
    uint32_t ImageCount() const { return static_cast<uint32_t>(m_images.size()); }

private:
    Swapchain(Device& device, const SwapchainOptions& options) : m_device(device), m_options(options) {}
    Result<void> Build(VkExtent2D requested);
    void DestroyViews();

    Device& m_device;
    SwapchainOptions m_options;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_views;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
    bool m_needsRecreate = false;
};

}
