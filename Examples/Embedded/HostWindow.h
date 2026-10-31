#pragma once

#include <volk.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

struct GLFWwindow;

namespace Embedded {

// Thrown when this machine cannot run the host at all: no display, no Vulkan loader, or no device
// that offers Vulkan 1.3 with the features Onyx needs. Main turns it into exit code 77.
struct NoDevice : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct SwapImage {
    uint32_t index = 0;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

// The host's side of the embedding: a GLFW window, a Vulkan instance, device, queues and swapchain
// created by hand through volk. Onyx sees none of this except the handles passed to
// Gpu::Device::Adopt. Every failing VkResult throws std::runtime_error. Main thread only.
class HostWindow {
public:
    HostWindow(const char* title, int width, int height);
    ~HostWindow();

    HostWindow(const HostWindow&) = delete;
    HostWindow& operator=(const HostWindow&) = delete;

    GLFWwindow* Window() const { return m_window; }
    VkInstance Instance() const { return m_instance; }
    VkPhysicalDevice PhysicalDevice() const { return m_physical; }
    VkDevice Device() const { return m_device; }
    uint32_t QueueFamily() const { return m_family; }
    // The queue handed to Onyx. It is the host's own queue when the family has only one.
    VkQueue OnyxQueue() const { return m_onyxQueue; }
    bool SharesQueue() const { return m_onyxQueue == m_presentQueue; }
    const char* DeviceName() const { return m_deviceName; }

    VkFormat Format() const { return m_format; }
    uint32_t ImageCount() const { return static_cast<uint32_t>(m_images.size()); }
    VkExtent2D Extent() const { return m_extent; }
    bool Minimised() const;

    // Recreates the swapchain when the framebuffer size changed or the last present said it is stale.
    void Refresh();
    // False when the swapchain is out of date: the frame is skipped. On success `acquired` is
    // signalled when the image is ready and Present waits on the semaphore for that image.
    bool Acquire(uint32_t slot, SwapImage& image, VkSemaphore& acquired);
    VkSemaphore PresentSemaphore(const SwapImage& image) const { return m_present[image.index]; }
    void Present(const SwapImage& image);

    // Records the layout changes and the dynamic-rendering scope around the ImGui pass.
    void BeginRendering(VkCommandBuffer cmd, const SwapImage& image) const;
    void EndRendering(VkCommandBuffer cmd, const SwapImage& image) const;

    void WaitIdle();

private:
    void Destroy();
    void CreateInstance();
    void PickDevice();
    void CreateDevice();
    void CreateSwapchain();
    void DestroySwapchain();
    VkSemaphore MakeSemaphore();

    GLFWwindow* m_window = nullptr;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    uint32_t m_family = 0;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    VkQueue m_onyxQueue = VK_NULL_HANDLE;
    char m_deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE]{};

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_views;
    std::vector<VkSemaphore> m_acquire;
    std::vector<VkSemaphore> m_present;
    bool m_stale = false;
};

}
