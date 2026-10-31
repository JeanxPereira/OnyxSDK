#include "Onyx/Gpu/Device.h"

#include <cstring>
#include <format>
#include <fstream>
#include <type_traits>

namespace Onyx::Gpu {

namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

std::once_flag g_volkOnce;
VkResult g_volkResult = VK_ERROR_INITIALIZATION_FAILED;
std::mutex g_tableMutex;

VkResult EnsureVolk(PFN_vkGetInstanceProcAddr custom) {
    std::call_once(g_volkOnce, [custom] {
        if (custom != nullptr) {
            volkInitializeCustom(custom);
            g_volkResult = VK_SUCCESS;
        } else {
            g_volkResult = volkInitialize();
        }
    });
    return g_volkResult;
}

Error VkFail(const char* code, const char* what, VkResult result) {
    return {code, std::format("{} failed (VkResult {})", what, static_cast<int>(result)), static_cast<int32_t>(result)};
}

// volkLoadDeviceTable resolves through the global vkGetDeviceProcAddr, so the
// pointer the caller owns is swapped in for the duration of the load.
void LoadTable(VolkDeviceTable& table, VkDevice device, PFN_vkGetDeviceProcAddr getDeviceProcAddr) {
    std::lock_guard lock(g_tableMutex);
    PFN_vkGetDeviceProcAddr previous = ::vkGetDeviceProcAddr;
    ::vkGetDeviceProcAddr = getDeviceProcAddr;
    volkLoadDeviceTable(&table, device);
    ::vkGetDeviceProcAddr = previous;
}

bool LayerAvailable(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    if (count != 0) {
        vkEnumerateInstanceLayerProperties(&count, layers.data());
    }
    for (const VkLayerProperties& layer : layers) {
        if (std::strcmp(layer.layerName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool InstanceExtensionAvailable(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    if (count != 0) {
        vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    }
    for (const VkExtensionProperties& e : extensions) {
        if (std::strcmp(e.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool DeviceExtensionAvailable(VkPhysicalDevice physical, const char* name) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    if (count != 0) {
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data());
    }
    for (const VkExtensionProperties& e : extensions) {
        if (std::strcmp(e.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

struct FeatureQuery {
    VkPhysicalDeviceVulkan12Features v12{};
    VkPhysicalDeviceVulkan13Features v13{};
    VkPhysicalDeviceFeatures2 features2{};

    FeatureQuery() {
        v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        v12.pNext = &v13;
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &v12;
    }
    FeatureQuery(const FeatureQuery&) = delete;
    FeatureQuery& operator=(const FeatureQuery&) = delete;

    bool Complete() const {
        return v13.dynamicRendering && v13.synchronization2 && v12.timelineSemaphore;
    }
};

struct Candidate {
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkPhysicalDeviceProperties props{};
};

}

Result<std::unique_ptr<Device>> Device::Create(const DeviceOptions& options) {
    VkResult loaded = EnsureVolk(nullptr);
    if (loaded != VK_SUCCESS) {
        return Result<std::unique_ptr<Device>>::Fail(
            {"gpu.noDevice", "no Vulkan loader found (vulkan-1.dll / libvulkan.so.1)", static_cast<int32_t>(loaded)});
    }
    std::unique_ptr<Device> device(new Device());
    Result<void> created = device->CreateOwned(options);
    if (!created.ok()) {
        return Result<std::unique_ptr<Device>>::Fail(created.error());
    }
    return Result<std::unique_ptr<Device>>::Ok(std::move(device));
}

Result<std::unique_ptr<Device>> Device::Adopt(const ExternalDevice& external) {
    std::unique_ptr<Device> device(new Device());
    Result<void> adopted = device->AdoptExternal(external);
    if (!adopted.ok()) {
        return Result<std::unique_ptr<Device>>::Fail(adopted.error());
    }
    return Result<std::unique_ptr<Device>>::Ok(std::move(device));
}

void Device::LoadInstanceFns(PFN_vkGetInstanceProcAddr getInstanceProcAddr) {
    auto load = [&](auto& slot, const char* name) {
        slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(getInstanceProcAddr(m_instance, name));
    };
    load(m_instanceFn.getPhysicalDeviceProperties2, "vkGetPhysicalDeviceProperties2");
    load(m_instanceFn.getPhysicalDeviceFeatures2, "vkGetPhysicalDeviceFeatures2");
    load(m_instanceFn.getPhysicalDeviceQueueFamilyProperties, "vkGetPhysicalDeviceQueueFamilyProperties");
    load(m_instanceFn.getPhysicalDeviceSurfaceSupportKHR, "vkGetPhysicalDeviceSurfaceSupportKHR");
    load(m_instanceFn.getPhysicalDeviceSurfaceCapabilitiesKHR, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    load(m_instanceFn.getPhysicalDeviceSurfaceFormatsKHR, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    load(m_instanceFn.getPhysicalDeviceSurfacePresentModesKHR, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    load(m_instanceFn.destroySurfaceKHR, "vkDestroySurfaceKHR");
    load(m_instanceFn.createDebugUtilsMessengerEXT, "vkCreateDebugUtilsMessengerEXT");
    load(m_instanceFn.destroyDebugUtilsMessengerEXT, "vkDestroyDebugUtilsMessengerEXT");
    load(m_instanceFn.destroyInstance, "vkDestroyInstance");
    load(m_instanceFn.getPhysicalDeviceFormatProperties, "vkGetPhysicalDeviceFormatProperties");
    load(m_instanceFn.getPhysicalDeviceMemoryProperties, "vkGetPhysicalDeviceMemoryProperties");
}

Result<void> Device::CreateOwned(const DeviceOptions& options) {
    m_owned = true;
    m_getInstanceProcAddr = ::vkGetInstanceProcAddr;

    std::vector<const char*> extensions;
    auto addExtension = [&](const char* name) {
        for (const char* existing : extensions) {
            if (std::strcmp(existing, name) == 0) {
                return;
            }
        }
        extensions.push_back(name);
    };
    if (options.presentSupport) {
        addExtension(VK_KHR_SURFACE_EXTENSION_NAME);
#if defined(VK_USE_PLATFORM_WIN32_KHR)
        addExtension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
    }
    for (const char* name : options.instanceExtensions) {
        addExtension(name);
    }

    std::vector<const char*> layers;
    bool debugUtils = false;
    bool validationFeatures = false;
    if (options.validation && LayerAvailable(kValidationLayer)) {
        layers.push_back(kValidationLayer);
        m_info.validation = true;
        if (InstanceExtensionAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            addExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            debugUtils = true;
        }
        if (InstanceExtensionAvailable(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME)) {
            addExtension(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
            validationFeatures = true;
        }
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = options.applicationName.c_str();
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "Onyx";
    app.engineVersion = VK_MAKE_VERSION(2, 0, 0);
    app.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &app;
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    instanceInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    instanceInfo.ppEnabledLayerNames = layers.data();

    const VkValidationFeatureEnableEXT syncValidation = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validationInfo{};
    if (validationFeatures) {
        validationInfo.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        validationInfo.enabledValidationFeatureCount = 1;
        validationInfo.pEnabledValidationFeatures = &syncValidation;
        instanceInfo.pNext = &validationInfo;
    }

    VkResult r = vkCreateInstance(&instanceInfo, nullptr, &m_instance);
    if (r != VK_SUCCESS) {
        m_instance = VK_NULL_HANDLE;
        const bool none = r == VK_ERROR_INCOMPATIBLE_DRIVER || r == VK_ERROR_INITIALIZATION_FAILED;
        return Result<void>::Fail(VkFail(none ? "gpu.noDevice" : "gpu.instance", "vkCreateInstance", r));
    }
    volkLoadInstance(m_instance);
    LoadInstanceFns(::vkGetInstanceProcAddr);

    if (debugUtils && m_instanceFn.createDebugUtilsMessengerEXT != nullptr) {
        VkDebugUtilsMessengerCreateInfoEXT messenger{};
        messenger.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messenger.pfnUserCallback = &Device::DebugCallback;
        messenger.pUserData = this;
        r = m_instanceFn.createDebugUtilsMessengerEXT(m_instance, &messenger, nullptr, &m_messenger);
        if (r != VK_SUCCESS) {
            m_messenger = VK_NULL_HANDLE;
            return Result<void>::Fail(VkFail("gpu.instance", "vkCreateDebugUtilsMessengerEXT", r));
        }
    }

    uint32_t physicalCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &physicalCount, nullptr);
    std::vector<VkPhysicalDevice> physicals(physicalCount);
    if (physicalCount != 0) {
        vkEnumeratePhysicalDevices(m_instance, &physicalCount, physicals.data());
    }

    auto qualify = [&](VkPhysicalDevice physical, Candidate& out) {
        if (options.presentSupport && !DeviceExtensionAvailable(physical, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            return false;
        }
        uint32_t familyCount = 0;
        m_instanceFn.getPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        m_instanceFn.getPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
        for (uint32_t f = 0; f < familyCount; ++f) {
            if ((families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                continue;
            }
#if defined(VK_USE_PLATFORM_WIN32_KHR)
            if (options.presentSupport && vkGetPhysicalDeviceWin32PresentationSupportKHR != nullptr &&
                !vkGetPhysicalDeviceWin32PresentationSupportKHR(physical, f)) {
                continue;
            }
#endif
            out.physical = physical;
            out.family = f;
            vkGetPhysicalDeviceProperties(physical, &out.props);
            return true;
        }
        return false;
    };

    Candidate chosen;
    bool found = false;
    if (options.physicalDeviceIndex.has_value()) {
        const uint32_t index = *options.physicalDeviceIndex;
        found = index < physicals.size() && qualify(physicals[index], chosen);
    } else {
        Candidate first;
        bool haveFirst = false;
        for (VkPhysicalDevice physical : physicals) {
            Candidate candidate;
            if (!qualify(physical, candidate)) {
                continue;
            }
            if (candidate.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                chosen = candidate;
                found = true;
                break;
            }
            if (!haveFirst) {
                first = candidate;
                haveFirst = true;
            }
        }
        if (!found && haveFirst) {
            chosen = first;
            found = true;
        }
    }
    if (!found) {
        return Result<void>::Fail({"gpu.noDevice", "no physical device offers a graphics queue family for these options", 0});
    }

    m_physical = chosen.physical;
    m_info.deviceName = chosen.props.deviceName;
    m_info.apiVersion = chosen.props.apiVersion;
    m_info.deviceType = chosen.props.deviceType;
    if (chosen.props.apiVersion < VK_API_VERSION_1_3) {
        return Result<void>::Fail({"gpu.feature", std::format("'{}' does not support Vulkan 1.3", m_info.deviceName), 0});
    }

    FeatureQuery supported;
    m_instanceFn.getPhysicalDeviceFeatures2(m_physical, &supported.features2);
    if (!supported.Complete()) {
        return Result<void>::Fail({"gpu.feature",
                                   std::format("'{}' lacks dynamicRendering, synchronization2 or timelineSemaphore", m_info.deviceName),
                                   0});
    }

    FeatureQuery enabled;
    enabled.v12.timelineSemaphore = VK_TRUE;
    enabled.v13.dynamicRendering = VK_TRUE;
    enabled.v13.synchronization2 = VK_TRUE;
    enabled.features2.features.robustBufferAccess = supported.features2.features.robustBufferAccess;
    m_info.robustBufferAccess = supported.features2.features.robustBufferAccess == VK_TRUE;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = chosen.family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    std::vector<const char*> deviceExtensions;
    if (options.presentSupport) {
        deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &enabled.features2;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

    r = vkCreateDevice(m_physical, &deviceInfo, nullptr, &m_device);
    if (r != VK_SUCCESS) {
        m_device = VK_NULL_HANDLE;
        return Result<void>::Fail(VkFail("gpu.device", "vkCreateDevice", r));
    }
    m_getDeviceProcAddr = ::vkGetDeviceProcAddr;
    LoadTable(m_fn, m_device, m_getDeviceProcAddr);

    VkQueue queue = VK_NULL_HANDLE;
    m_fn.vkGetDeviceQueue(m_device, chosen.family, 0, &queue);
    m_queue.reset(new Queue(*this, queue, chosen.family));
    return FinishInit(options.pipelineCachePath);
}

Result<void> Device::AdoptExternal(const ExternalDevice& external) {
    if (external.instance == VK_NULL_HANDLE || external.physicalDevice == VK_NULL_HANDLE ||
        external.device == VK_NULL_HANDLE || external.queue == VK_NULL_HANDLE ||
        external.getInstanceProcAddr == nullptr || external.getDeviceProcAddr == nullptr) {
        return Result<void>::Fail({"gpu.adopt", "ExternalDevice is missing a handle or a proc address loader", 0});
    }
    if (!external.enabled.dynamicRendering || !external.enabled.synchronization2 || !external.enabled.timelineSemaphore) {
        return Result<void>::Fail({"gpu.feature",
                                   "the host must enable dynamicRendering, synchronization2 and timelineSemaphore", 0});
    }
    EnsureVolk(external.getInstanceProcAddr);

    m_owned = false;
    m_instance = external.instance;
    m_physical = external.physicalDevice;
    m_device = external.device;
    m_getInstanceProcAddr = external.getInstanceProcAddr;
    m_getDeviceProcAddr = external.getDeviceProcAddr;
    LoadInstanceFns(external.getInstanceProcAddr);
    if (m_instanceFn.getPhysicalDeviceFeatures2 == nullptr || m_instanceFn.getPhysicalDeviceProperties2 == nullptr) {
        m_device = VK_NULL_HANDLE;
        return Result<void>::Fail({"gpu.adopt", "the host instance does not resolve Vulkan 1.1 physical device queries", 0});
    }

    VkPhysicalDeviceProperties2 props{};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    m_instanceFn.getPhysicalDeviceProperties2(m_physical, &props);
    m_info.deviceName = props.properties.deviceName;
    m_info.apiVersion = props.properties.apiVersion;
    m_info.deviceType = props.properties.deviceType;
    m_info.adopted = true;

    FeatureQuery supported;
    m_instanceFn.getPhysicalDeviceFeatures2(m_physical, &supported.features2);
    if (props.properties.apiVersion < VK_API_VERSION_1_3 || !supported.Complete()) {
        m_device = VK_NULL_HANDLE;
        return Result<void>::Fail({"gpu.feature",
                                   std::format("'{}' does not support the features the host claims to have enabled", m_info.deviceName),
                                   0});
    }

    LoadTable(m_fn, m_device, m_getDeviceProcAddr);
    m_queue.reset(new Queue(*this, external.queue, external.queueFamily));
    return FinishInit(external.pipelineCachePath);
}

Result<void> Device::FinishInit(const std::filesystem::path& pipelineCachePath) {
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = m_getInstanceProcAddr;
    functions.vkGetDeviceProcAddr = m_getDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = m_physical;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.pVulkanFunctions = &functions;
    VkResult r = vmaCreateAllocator(&allocatorInfo, &m_allocator);
    if (r != VK_SUCCESS) {
        m_allocator = VK_NULL_HANDLE;
        return Result<void>::Fail(VkFail("gpu.alloc", "vmaCreateAllocator", r));
    }

    Result<void> timeline = m_queue->Init();
    if (!timeline.ok()) {
        return timeline;
    }

    m_pipelineCachePath = pipelineCachePath;
    return CreatePipelineCache();
}

Result<void> Device::CreatePipelineCache() {
    std::vector<char> data;
    std::error_code ec;
    if (!m_pipelineCachePath.empty() && std::filesystem::is_regular_file(m_pipelineCachePath, ec)) {
        std::ifstream file(m_pipelineCachePath, std::ios::binary);
        if (file) {
            data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
    }

    VkPipelineCacheCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    info.initialDataSize = data.size();
    info.pInitialData = data.empty() ? nullptr : data.data();
    VkResult r = m_fn.vkCreatePipelineCache(m_device, &info, nullptr, &m_pipelineCache);
    if (r != VK_SUCCESS && !data.empty()) {
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
        r = m_fn.vkCreatePipelineCache(m_device, &info, nullptr, &m_pipelineCache);
    }
    if (r != VK_SUCCESS) {
        m_pipelineCache = VK_NULL_HANDLE;
        return Result<void>::Fail(VkFail("gpu.pipelineCache", "vkCreatePipelineCache", r));
    }
    return Result<void>::Ok();
}

void Device::SavePipelineCache() const {
    if (m_pipelineCache == VK_NULL_HANDLE || m_pipelineCachePath.empty()) {
        return;
    }
    size_t size = 0;
    if (m_fn.vkGetPipelineCacheData(m_device, m_pipelineCache, &size, nullptr) != VK_SUCCESS || size == 0) {
        return;
    }
    std::vector<char> data(size);
    if (m_fn.vkGetPipelineCacheData(m_device, m_pipelineCache, &size, data.data()) != VK_SUCCESS) {
        return;
    }

    std::error_code ec;
    if (m_pipelineCachePath.has_parent_path()) {
        std::filesystem::create_directories(m_pipelineCachePath.parent_path(), ec);
    }
    auto temporary = m_pipelineCachePath;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return;
        }
        file.write(data.data(), static_cast<std::streamsize>(size));
        if (!file) {
            file.close();
            std::filesystem::remove(temporary, ec);
            return;
        }
    }
    std::filesystem::rename(temporary, m_pipelineCachePath, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
    }
}

Device::~Device() {
    if (m_device != VK_NULL_HANDLE) {
        m_fn.vkDeviceWaitIdle(m_device);
    }
    m_retire.Drain();
    if (m_device != VK_NULL_HANDLE) {
        SavePipelineCache();
        if (m_pipelineCache != VK_NULL_HANDLE) {
            m_fn.vkDestroyPipelineCache(m_device, m_pipelineCache, nullptr);
        }
    }
    m_queue.reset();
    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
    }
    if (m_owned) {
        if (m_device != VK_NULL_HANDLE) {
            m_fn.vkDestroyDevice(m_device, nullptr);
        }
        if (m_messenger != VK_NULL_HANDLE) {
            m_instanceFn.destroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
        }
        if (m_instance != VK_NULL_HANDLE) {
            m_instanceFn.destroyInstance(m_instance, nullptr);
        }
    }
}

VkBool32 VKAPI_CALL Device::DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                                          const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    auto* device = static_cast<Device*>(user);
    std::lock_guard lock(device->m_validationMutex);
    ++device->m_validationCount;
    device->m_lastValidation = (data != nullptr && data->pMessage != nullptr) ? data->pMessage : "";
    return VK_FALSE;
}

uint32_t Device::ValidationMessageCount() const {
    std::lock_guard lock(m_validationMutex);
    return m_validationCount;
}

std::string Device::LastValidationMessage() const {
    std::lock_guard lock(m_validationMutex);
    return m_lastValidation;
}

}
