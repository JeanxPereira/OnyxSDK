#include "HostWindow.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace Embedded {

namespace {

void Check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed (VkResult " + std::to_string(result) + ")");
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL OnMessage(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                                         const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    std::fprintf(stderr, "[vulkan] %s\n", data->pMessage);
    ++*static_cast<std::atomic<uint32_t>*>(user);
    return VK_FALSE;
}

struct Features {
    VkPhysicalDeviceVulkan12Features v12{};
    VkPhysicalDeviceVulkan13Features v13{};
    VkPhysicalDeviceFeatures2 features2{};

    Features() {
        v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        v12.pNext = &v13;
        features2.pNext = &v12;
    }
    Features(const Features&) = delete;
    Features& operator=(const Features&) = delete;

    bool Complete() const { return v12.timelineSemaphore && v13.dynamicRendering && v13.synchronization2; }
};

void Transition(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

}

HostWindow::HostWindow(const char* title, int width, int height) {
    if (!glfwInit()) {
        throw NoDevice("glfwInit failed: no display");
    }
    try {
        if (!glfwVulkanSupported()) {
            throw NoDevice("GLFW finds no Vulkan loader");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (m_window == nullptr) {
            throw std::runtime_error("glfwCreateWindow failed");
        }
        CreateInstance();
        Check(glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface), "glfwCreateWindowSurface");
        PickDevice();
        CreateDevice();
        CreateSwapchain();
    } catch (...) {
        Destroy();
        throw;
    }
}

HostWindow::~HostWindow() {
    Destroy();
}

void HostWindow::Destroy() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
        DestroySwapchain();
        for (VkSemaphore semaphore : m_acquire) {
            vkDestroySemaphore(m_device, semaphore, nullptr);
        }
        m_acquire.clear();
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    if (m_messenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
        m_messenger = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
    if (m_window != nullptr) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
}

void HostWindow::CreateInstance() {
    if (volkInitialize() != VK_SUCCESS) {
        throw NoDevice("no Vulkan loader found");
    }

    uint32_t extensionCount = 0;
    const char** required = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (required == nullptr) {
        throw NoDevice("GLFW cannot present with Vulkan here");
    }
    std::vector<const char*> extensions(required, required + extensionCount);
    std::vector<const char*> layers;
    bool debugUtils = false;

    // The validation layer, when installed, checks the host and Onyx alike: the adopted device lives on
    // this instance.
    uint32_t layerCount = 0;
    Check(vkEnumerateInstanceLayerProperties(&layerCount, nullptr), "vkEnumerateInstanceLayerProperties");
    std::vector<VkLayerProperties> available(layerCount);
    Check(vkEnumerateInstanceLayerProperties(&layerCount, available.data()), "vkEnumerateInstanceLayerProperties");
    const bool hasValidation = std::any_of(available.begin(), available.end(), [](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
    });
    if (hasValidation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        debugUtils = true;
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Embedded";
    app.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    const VkResult created = vkCreateInstance(&info, nullptr, &m_instance);
    if (created == VK_ERROR_INCOMPATIBLE_DRIVER || created == VK_ERROR_INITIALIZATION_FAILED) {
        m_instance = VK_NULL_HANDLE;
        throw NoDevice("no Vulkan 1.3 driver");
    }
    Check(created, "vkCreateInstance");
    volkLoadInstance(m_instance);

    if (debugUtils) {
        VkDebugUtilsMessengerCreateInfoEXT messenger{};
        messenger.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messenger.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messenger.pfnUserCallback = &OnMessage;
        messenger.pUserData = &m_validationMessages;
        Check(vkCreateDebugUtilsMessengerEXT(m_instance, &messenger, nullptr, &m_messenger),
              "vkCreateDebugUtilsMessengerEXT");
    }
}

void HostWindow::PickDevice() {
    uint32_t count = 0;
    Check(vkEnumeratePhysicalDevices(m_instance, &count, nullptr), "vkEnumeratePhysicalDevices");
    std::vector<VkPhysicalDevice> devices(count);
    Check(vkEnumeratePhysicalDevices(m_instance, &count, devices.data()), "vkEnumeratePhysicalDevices");

    bool found = false;
    bool foundDiscrete = false;
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        if (props.apiVersion < VK_API_VERSION_1_3) {
            continue;
        }
        Features features;
        vkGetPhysicalDeviceFeatures2(device, &features.features2);
        if (!features.Complete()) {
            continue;
        }

        uint32_t extensionCount = 0;
        Check(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr),
              "vkEnumerateDeviceExtensionProperties");
        std::vector<VkExtensionProperties> extensions(extensionCount);
        Check(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data()),
              "vkEnumerateDeviceExtensionProperties");
        const bool swapchain = std::any_of(extensions.begin(), extensions.end(), [](const VkExtensionProperties& e) {
            return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
        });
        if (!swapchain) {
            continue;
        }

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
        for (uint32_t f = 0; f < familyCount; ++f) {
            if ((families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                continue;
            }
            VkBool32 presents = VK_FALSE;
            Check(vkGetPhysicalDeviceSurfaceSupportKHR(device, f, m_surface, &presents),
                  "vkGetPhysicalDeviceSurfaceSupportKHR");
            if (!presents) {
                continue;
            }
            const bool discrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            if (!found || (discrete && !foundDiscrete)) {
                m_physical = device;
                m_family = f;
                std::strncpy(m_deviceName, props.deviceName, sizeof(m_deviceName) - 1);
                found = true;
                foundDiscrete = discrete;
            }
            break;
        }
    }
    if (!found) {
        throw NoDevice("no Vulkan 1.3 device with dynamic rendering, synchronization2 and a presenting graphics queue");
    }
}

void HostWindow::CreateDevice() {
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physical, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physical, &familyCount, families.data());
    // Onyx wants a queue of its own; the host presents on the first one. A family with a single queue
    // makes the two share it. That is correct only single-threaded: the host's vkQueuePresentKHR does not
    // take Onyx's queue mutex, so nothing may submit through Onyx from another thread meanwhile.
    const uint32_t queueCount = families[m_family].queueCount >= 2 ? 2 : 1;
    const float priorities[2] = {1.0f, 1.0f};

    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = m_family;
    queueInfo.queueCount = queueCount;
    queueInfo.pQueuePriorities = priorities;

    Features supported;
    vkGetPhysicalDeviceFeatures2(m_physical, &supported.features2);
    Features enabled;
    enabled.v12.timelineSemaphore = VK_TRUE;
    enabled.v13.dynamicRendering = VK_TRUE;
    enabled.v13.synchronization2 = VK_TRUE;
    enabled.features2.features.samplerAnisotropy = supported.features2.features.samplerAnisotropy;
    enabled.features2.features.robustBufferAccess = supported.features2.features.robustBufferAccess;

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.pNext = &enabled.features2;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = extensions;
    Check(vkCreateDevice(m_physical, &info, nullptr, &m_device), "vkCreateDevice");
    volkLoadDevice(m_device);

    vkGetDeviceQueue(m_device, m_family, 0, &m_presentQueue);
    vkGetDeviceQueue(m_device, m_family, queueCount - 1, &m_onyxQueue);
}

bool HostWindow::Minimised() const {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    return width <= 0 || height <= 0;
}

void HostWindow::CreateSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical, m_surface, &caps),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    uint32_t formatCount = 0;
    Check(vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical, m_surface, &formatCount, nullptr),
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    Check(vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical, m_surface, &formatCount, formats.data()),
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (formats.empty()) {
        throw std::runtime_error("the surface reports no formats");
    }
    VkSurfaceFormatKHR format = formats.front();
    for (const VkSurfaceFormatKHR& candidate : formats) {
        if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format = candidate;
            break;
        }
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        extent.width = std::clamp(static_cast<uint32_t>((std::max)(width, 1)), caps.minImageExtent.width,
                                  caps.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>((std::max)(height, 1)), caps.minImageExtent.height,
                                   caps.maxImageExtent.height);
    }
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) {
        imageCount = (std::min)(imageCount, caps.maxImageCount);
    }
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((caps.supportedCompositeAlpha & alpha) == 0) {
        const uint32_t supported = caps.supportedCompositeAlpha;
        alpha = static_cast<VkCompositeAlphaFlagBitsKHR>(supported & (~supported + 1u));
    }

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = m_surface;
    info.minImageCount = imageCount;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = alpha;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    info.oldSwapchain = m_swapchain;
    VkSwapchainKHR created = VK_NULL_HANDLE;
    Check(vkCreateSwapchainKHR(m_device, &info, nullptr, &created), "vkCreateSwapchainKHR");
    // The caller has drained the device, so the old swapchain, its views and its semaphores can go.
    DestroySwapchain();
    m_swapchain = created;
    m_format = format.format;
    m_extent = extent;

    uint32_t count = 0;
    Check(vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, nullptr), "vkGetSwapchainImagesKHR");
    m_images.resize(count);
    Check(vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, m_images.data()), "vkGetSwapchainImagesKHR");
    for (VkImage image : m_images) {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = m_format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView imageView = VK_NULL_HANDLE;
        Check(vkCreateImageView(m_device, &view, nullptr, &imageView), "vkCreateImageView");
        m_views.push_back(imageView);
        m_present.push_back(MakeSemaphore());
    }
    m_stale = false;
}

void HostWindow::DestroySwapchain() {
    for (VkImageView view : m_views) {
        vkDestroyImageView(m_device, view, nullptr);
    }
    m_views.clear();
    m_images.clear();
    for (VkSemaphore semaphore : m_present) {
        vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    m_present.clear();
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

VkSemaphore HostWindow::MakeSemaphore() {
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    Check(vkCreateSemaphore(m_device, &info, nullptr, &semaphore), "vkCreateSemaphore");
    return semaphore;
}

void HostWindow::Refresh() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }
    if (m_stale || static_cast<uint32_t>(width) != m_extent.width || static_cast<uint32_t>(height) != m_extent.height) {
        WaitIdle();
        CreateSwapchain();
    }
}

bool HostWindow::Acquire(uint32_t slot, SwapImage& image, VkSemaphore& acquired) {
    while (m_acquire.size() <= slot) {
        m_acquire.push_back(MakeSemaphore());
    }
    acquired = m_acquire[slot];
    uint32_t index = 0;
    const VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        m_stale = true;
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR) {
        m_stale = true;
    } else {
        Check(result, "vkAcquireNextImageKHR");
    }
    image = {index, m_images[index], m_views[index]};
    return true;
}

void HostWindow::Present(const SwapImage& image) {
    VkSemaphore wait = m_present[image.index];
    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &wait;
    info.swapchainCount = 1;
    info.pSwapchains = &m_swapchain;
    info.pImageIndices = &image.index;
    const VkResult result = vkQueuePresentKHR(m_presentQueue, &info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        m_stale = true;
        return;
    }
    Check(result, "vkQueuePresentKHR");
}

void HostWindow::BeginRendering(VkCommandBuffer cmd, const SwapImage& image) const {
    Transition(cmd, image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = image.view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = {{0.10f, 0.10f, 0.12f, 1.0f}};

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = {{0, 0}, m_extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    vkCmdBeginRendering(cmd, &rendering);

    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height),
                              0.0f,  1.0f};
    const VkRect2D scissor{{0, 0}, m_extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void HostWindow::EndRendering(VkCommandBuffer cmd, const SwapImage& image) const {
    vkCmdEndRendering(cmd);
    Transition(cmd, image.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
               VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE);
}

void HostWindow::WaitIdle() {
    Check(vkDeviceWaitIdle(m_device), "vkDeviceWaitIdle");
}

}
