#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "Onyx/Core/Result.h"
#include "Onyx/Gpu/Queue.h"
#include "Onyx/Gpu/Retire.h"

namespace Onyx::Gpu {

#ifndef NDEBUG
inline constexpr bool kValidationDefault = true;
#else
inline constexpr bool kValidationDefault = false;
#endif

struct DeviceOptions {
    bool presentSupport = false;
    std::vector<const char*> instanceExtensions;
    bool validation = kValidationDefault;
    std::optional<uint32_t> physicalDeviceIndex;
    std::filesystem::path pipelineCachePath;
    std::string applicationName = "Onyx";
};

struct ExternalFeatures {
    bool dynamicRendering = false;
    bool synchronization2 = false;
    bool timelineSemaphore = false;
};

struct ExternalDevice {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    ExternalFeatures enabled;
    std::filesystem::path pipelineCachePath;
};

struct DeviceInfo {
    std::string deviceName;
    uint32_t apiVersion = 0;
    VkPhysicalDeviceType deviceType = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    bool validation = false;
    bool adopted = false;
    bool robustBufferAccess = false;
};

struct InstanceFns {
    PFN_vkGetPhysicalDeviceProperties2 getPhysicalDeviceProperties2 = nullptr;
    PFN_vkGetPhysicalDeviceFeatures2 getPhysicalDeviceFeatures2 = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties getPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR getPhysicalDeviceSurfaceSupportKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR getPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR getPhysicalDeviceSurfaceFormatsKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR getPhysicalDeviceSurfacePresentModesKHR = nullptr;
    PFN_vkDestroySurfaceKHR destroySurfaceKHR = nullptr;
    PFN_vkCreateDebugUtilsMessengerEXT createDebugUtilsMessengerEXT = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugUtilsMessengerEXT = nullptr;
    PFN_vkDestroyInstance destroyInstance = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties getPhysicalDeviceFormatProperties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties getPhysicalDeviceMemoryProperties = nullptr;
};

class Device {
public:
    static Result<std::unique_ptr<Device>> Create(const DeviceOptions& options);
    static Result<std::unique_ptr<Device>> Adopt(const ExternalDevice& external);

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    ~Device();

    const VolkDeviceTable& Fn() const { return m_fn; }
    const InstanceFns& InstanceFn() const { return m_instanceFn; }
    VkInstance Instance() const { return m_instance; }
    VkPhysicalDevice PhysicalDevice() const { return m_physical; }
    VkDevice Handle() const { return m_device; }
    VmaAllocator Allocator() const { return m_allocator; }
    Queue& GraphicsQueue() { return *m_queue; }
    RetireQueue& Retire() { return m_retire; }
    VkPipelineCache PipelineCache() const { return m_pipelineCache; }
    const DeviceInfo& Info() const { return m_info; }
    uint32_t ValidationMessageCount() const;
    std::string LastValidationMessage() const;
    uint64_t SubmitCount() const { return m_submitCount.load(); }

private:
    friend class Queue;
    Device() = default;

    Result<void> CreateOwned(const DeviceOptions& options);
    Result<void> AdoptExternal(const ExternalDevice& external);
    Result<void> FinishInit(const std::filesystem::path& pipelineCachePath);
    Result<void> CreatePipelineCache();
    void SavePipelineCache() const;
    void LoadInstanceFns(PFN_vkGetInstanceProcAddr getInstanceProcAddr);
    void CountSubmit() { ++m_submitCount; }

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                        VkDebugUtilsMessageTypeFlagsEXT types,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                        void* user);

    VolkDeviceTable m_fn{};
    InstanceFns m_instanceFn;
    DeviceInfo m_info;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
    std::filesystem::path m_pipelineCachePath;
    PFN_vkGetInstanceProcAddr m_getInstanceProcAddr = nullptr;
    PFN_vkGetDeviceProcAddr m_getDeviceProcAddr = nullptr;
    bool m_owned = false;
    RetireQueue m_retire;
    std::unique_ptr<Queue> m_queue;
    std::atomic<uint64_t> m_submitCount{0};
    mutable std::mutex m_validationMutex;
    uint32_t m_validationCount = 0;
    std::string m_lastValidation;
};

}
