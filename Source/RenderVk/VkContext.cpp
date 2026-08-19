#include <Onyx/RenderVk/VkContext.h>

#include <cstring>
#include <vector>

namespace Onyx::Rendering {

namespace {

constexpr uint32_t kApiVersion = VK_API_VERSION_1_3;

bool LayerAvailable(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    if (count) vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const VkLayerProperties& l : layers)
        if (std::strcmp(l.layerName, name) == 0) return true;
    return false;
}

bool DeviceExtensionAvailable(VkPhysicalDevice pd, const char* name) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    if (count) vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, exts.data());
    for (const VkExtensionProperties& e : exts)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

// F5 fix: VK_EXT_debug_utils and VK_EXT_validation_features were being
// requested purely on LayerAvailable("VK_LAYER_KHRONOS_validation"), with
// no check that the INSTANCE actually carries either extension. The
// validation layer usually brings both along, but "usually" is not a
// guarantee vkCreateInstance honors -- requesting an absent extension is
// VK_ERROR_EXTENSION_NOT_PRESENT, a hard Init failure, which would have
// contradicted the very next comment's "never turns a working Init into a
// failing one" promise the moment a driver/layer combo omitted either.
bool InstanceExtensionAvailable(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    if (count) vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
    for (const VkExtensionProperties& e : exts)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

// A family is suitable when it supports graphics and, if presentSupport was
// requested, the platform windowing system. The Win32 presentation query
// takes no VkSurfaceKHR -- it answers "could this queue family ever present
// to a Win32 window", which is exactly what a surface-less boot can check.
//
// T9 rider: the portable equivalent of that pre-surface query is GLFW's own
// glfwGetPhysicalDevicePresentationSupport(instance, pd, family) -- but
// VkContext must never include GLFW (this directory's binding rule: no
// GLFW in Onyx_Render sources, see the plan's Global Constraints and the
// task-9 brief). So on non-Windows platforms this function stays
// permissive at the device-selection stage; presentSupport there only
// gates the VK_KHR_swapchain extension request below. The REAL check for
// those platforms happens one layer up, once Window.cpp has a live
// VkSurfaceKHR: initVulkan() calls vkGetPhysicalDeviceSurfaceSupportKHR
// against the actual surface right after creating it and logs (does not
// yet hard-fail) a mismatch. That is later than device/queue-family
// selection would ideally catch it, but it is the earliest point in the
// boot sequence that does not require leaking GLFW into this file.
//
// There never was a Linux branch here -- the #else two lines below
// QueueFamilySuitable's body is (and always was) the permissive no-op
// fall-through documented above, not a platform-specific implementation.
// What Linux/GUI presentation actually lacked, until the F1 fix round,
// was the INSTANCE-level VK_KHR_surface + platform surface extension
// (VK_KHR_xcb_surface/VK_KHR_wayland_surface, whichever GLFW picks) --
// see Init's extraInstanceExtensions parameter below and Window.cpp's
// glfwGetRequiredInstanceExtensions() call feeding it. Verified end to
// end on Windows only (this build/test environment has no Linux GPU);
// the Linux path is implemented and builds in CI (linux-lavapipe) but
// has never been run against a real window.
bool QueueFamilySuitable(VkPhysicalDevice pd, uint32_t family,
                         const VkQueueFamilyProperties& props, bool presentSupport) {
    if (!(props.queueFlags & VK_QUEUE_GRAPHICS_BIT)) return false;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    if (presentSupport) {
        if (!vkGetPhysicalDeviceWin32PresentationSupportKHR) return false;
        if (!vkGetPhysicalDeviceWin32PresentationSupportKHR(pd, family)) return false;
    }
#else
    (void)pd; (void)presentSupport;
#endif
    return true;
}

} // namespace

bool VkContext::Init(bool presentSupport, std::string& err,
                      const std::vector<const char*>& extraInstanceExtensions) {
    if (m_device != VK_NULL_HANDLE) {
        err = "VkContext::Init called on an already-initialised context";
        return false;
    }

    // Reset per-session validation state here (not in Shutdown) so a
    // Shutdown'd-then-reInit'd context starts clean, while Shutdown itself
    // can still add to the count/message a caller reads afterward.
    m_validationMessageCount = 0;
    m_lastValidationMessage.clear();

    if (volkInitialize() != VK_SUCCESS) {
        err = "volkInitialize failed -- no Vulkan loader found "
              "(vulkan-1.dll on Windows, libvulkan.so.1 elsewhere)";
        return false;
    }

    // ---- Instance ----
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Onyx";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Onyx";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = kApiVersion;

    std::vector<const char*> instanceExtensions;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    if (presentSupport) {
        instanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    }
#endif
    // F1 fix: on any platform other than Win32, the two extensions above
    // were never requested at all -- the instance was created with no
    // surface extension, so a caller's later glfwCreateWindowSurface()
    // (Window.cpp) failed VK_ERROR_EXTENSION_NOT_PRESENT and the process
    // exited. VkContext itself must never link GLFW (this file's own
    // rule, see QueueFamilySuitable's comment above), so it cannot ask
    // GLFW what the current platform's surface needs; the caller passes
    // that list in here instead (Window.cpp: glfwGetRequiredInstanceExtensions()
    // before calling Init). De-duplicated against what VkContext already
    // queued (on Win32, glfwGetRequiredInstanceExtensions() returns
    // exactly VK_KHR_surface + VK_KHR_win32_surface -- the same two
    // pushed above -- so without this check Windows would ask for each
    // twice) and against itself, since vkCreateInstance rejects a
    // duplicated extension name.
    for (const char* ext : extraInstanceExtensions) {
        bool alreadyRequested = false;
        for (const char* existing : instanceExtensions) {
            if (std::strcmp(existing, ext) == 0) { alreadyRequested = true; break; }
        }
        if (!alreadyRequested) instanceExtensions.push_back(ext);
    }

    // Validation is an if-available convenience, never a requirement --
    // enabling it never turns a working Init into a failing one.
#ifndef NDEBUG
    const bool wantValidation = true;
#else
    const bool wantValidation = false;
#endif
    std::vector<const char*> instanceLayers;
    bool validationEnabled = false;
    bool validationFeaturesEnabled = false;
    if (wantValidation && LayerAvailable("VK_LAYER_KHRONOS_validation")) {
        instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
        validationEnabled = true;
        // VK_EXT_debug_utils is what lets the messenger below exist at all;
        // only requested alongside the layer that would actually emit
        // anything to it. F5 fix: LayerAvailable() above only proves the
        // LAYER exists, not that this INSTANCE extension does too -- they
        // usually travel together but nothing guarantees it, and
        // requesting an absent extension is VK_ERROR_EXTENSION_NOT_PRESENT,
        // a hard vkCreateInstance failure that would have contradicted
        // this very function's "never turns a working Init into a
        // failing one" promise (a few lines up) the moment a driver/SDK
        // combo omitted it. Guarded the same way DeviceExtensionAvailable()
        // already guards the device-level asks below.
        if (InstanceExtensionAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        // VK_EXT_validation_features is what makes the VkValidationFeaturesEXT
        // chained onto instInfo.pNext below (sync validation) actually take
        // effect -- the layer honors the pNext chain either way on most
        // drivers, but declaring the extension is what the spec requires.
        // Same availability guard; validationFeaturesEnabled gates whether
        // that pNext chain gets built at all below, since chaining a
        // struct for an extension the instance never declared is itself
        // exactly the kind of validation complaint this codebase treats
        // as a build-breaking regression.
        if (InstanceExtensionAvailable(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME)) {
            instanceExtensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
            validationFeaturesEnabled = true;
        }
    }

    VkInstanceCreateInfo instInfo{};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;
    instInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    instInfo.ppEnabledExtensionNames = instanceExtensions.data();
    instInfo.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size());
    instInfo.ppEnabledLayerNames = instanceLayers.data();

    // T10 fix-round-1: sync validation is the detection tool that would
    // have caught the write-after-read hazard this round fixes (a same-
    // queue write racing a prior frame's still-in-flight fragment-shader
    // read, with no semaphore/barrier tying the two submissions) -- it was
    // never enabled before, so the hazard shipped silently past
    // VkContext's own "0 validation messages" proof. Chained only when the
    // validation layer itself is present (same if-available convenience
    // as the layer/debug-messenger above); enabling it never turns a
    // working Init into a failing one, and it costs nothing when the layer
    // is absent (Release, or a machine without LunarG's SDK installed).
    // F5 fix: gated on validationFeaturesEnabled, not just validationEnabled
    // -- chaining this struct onto instInfo.pNext when
    // VK_EXT_validation_features was never actually requested (the layer
    // was present but the extension wasn't, per the availability check
    // above) would itself be a validation complaint about an undeclared
    // extension, or worse on a driver that enforces it strictly.
    VkValidationFeaturesEXT validationFeatures{};
    const VkValidationFeatureEnableEXT syncValidationFeature =
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    if (validationFeaturesEnabled) {
        validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        validationFeatures.enabledValidationFeatureCount = 1;
        validationFeatures.pEnabledValidationFeatures = &syncValidationFeature;
        instInfo.pNext = &validationFeatures;
    }

    VkResult vr = vkCreateInstance(&instInfo, nullptr, &m_instance);
    if (vr != VK_SUCCESS) {
        err = "vkCreateInstance failed (VkResult " + std::to_string(static_cast<int>(vr)) + ")";
        Shutdown();
        return false;
    }
    volkLoadInstance(m_instance);

    // ---- Debug messenger (instance-scoped; only when validation is on) ----
    if (validationEnabled && vkCreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT dbgInfo{};
        dbgInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbgInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgInfo.pfnUserCallback = &VkContext::DebugCallback;
        dbgInfo.pUserData = this;

        vr = vkCreateDebugUtilsMessengerEXT(m_instance, &dbgInfo, nullptr, &m_debugMessenger);
        if (vr != VK_SUCCESS) {
            err = "vkCreateDebugUtilsMessengerEXT failed (VkResult " +
                  std::to_string(static_cast<int>(vr)) + ")";
            Shutdown();
            return false;
        }
    }

    // ---- Physical device ----
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        err = "no Vulkan-capable physical devices enumerated";
        Shutdown();
        return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    uint32_t chosenFamily = UINT32_MAX;
    VkPhysicalDeviceProperties chosenProps{};

    // Pass 0: discrete GPUs only. Pass 1: any device at all. First hit wins.
    for (int pass = 0; pass < 2 && chosen == VK_NULL_HANDLE; ++pass) {
        for (VkPhysicalDevice pd : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(pd, &props);
            if (pass == 0 && props.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) continue;

            if (presentSupport && !DeviceExtensionAvailable(pd, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
                continue;

            uint32_t famCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &famCount, nullptr);
            std::vector<VkQueueFamilyProperties> fams(famCount);
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &famCount, fams.data());

            for (uint32_t f = 0; f < famCount; ++f) {
                if (QueueFamilySuitable(pd, f, fams[f], presentSupport)) {
                    chosen = pd;
                    chosenFamily = f;
                    chosenProps = props;
                    break;
                }
            }
            if (chosen != VK_NULL_HANDLE) break;
        }
    }

    if (chosen == VK_NULL_HANDLE) {
        err = presentSupport
            ? "no physical device exposes a graphics queue with platform "
              "presentation support and VK_KHR_swapchain"
            : "no physical device exposes a graphics-capable queue family";
        Shutdown();
        return false;
    }

    m_physical = chosen;
    m_graphicsFamily = chosenFamily;

    // ---- Vulkan 1.3 feature requirement ----
    VkPhysicalDeviceVulkan13Features supported13{};
    supported13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceFeatures2 supported2{};
    supported2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    supported2.pNext = &supported13;
    vkGetPhysicalDeviceFeatures2(m_physical, &supported2);

    if (!supported13.dynamicRendering || !supported13.synchronization2) {
        err = "physical device '" + std::string(chosenProps.deviceName) +
              "' lacks required Vulkan 1.3 features (dynamicRendering=" +
              (supported13.dynamicRendering ? "1" : "0") + " synchronization2=" +
              (supported13.synchronization2 ? "1" : "0") + ")";
        Shutdown();
        return false;
    }

    // ---- Device ----
    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = m_graphicsFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceVulkan13Features enable13{};
    enable13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    enable13.dynamicRendering = VK_TRUE;
    enable13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceFeatures2 enable2{};
    enable2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    enable2.pNext = &enable13;

    std::vector<const char*> deviceExtensions;
    if (presentSupport) deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &enable2;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

    vr = vkCreateDevice(m_physical, &deviceInfo, nullptr, &m_device);
    if (vr != VK_SUCCESS) {
        err = "vkCreateDevice failed (VkResult " + std::to_string(static_cast<int>(vr)) + ")";
        Shutdown();
        return false;
    }
    volkLoadDevice(m_device);

    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);

    // ---- VMA ----
    // VMA_STATIC_VULKAN_FUNCTIONS=0 (target define) means VMA calls nothing
    // directly; every entry point it needs is resolved from the two volk-
    // loaded pointers below (VMA_DYNAMIC_VULKAN_FUNCTIONS stays at its
    // header default of 1, which is what makes that resolution happen).
    VmaVulkanFunctions vmaFns{};
    vmaFns.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaFns.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocInfo{};
    allocInfo.physicalDevice = m_physical;
    allocInfo.device = m_device;
    allocInfo.instance = m_instance;
    allocInfo.vulkanApiVersion = kApiVersion;
    allocInfo.pVulkanFunctions = &vmaFns;

    vr = vmaCreateAllocator(&allocInfo, &m_allocator);
    if (vr != VK_SUCCESS) {
        err = "vmaCreateAllocator failed (VkResult " + std::to_string(static_cast<int>(vr)) + ")";
        Shutdown();
        return false;
    }

    m_info.deviceName = chosenProps.deviceName;
    m_info.apiVersion = kApiVersion;
    m_info.validation = validationEnabled;
    return true;
}

void VkContext::Shutdown() {
    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    // Destroyed after the device but before the instance it was created
    // against -- any validation raised while tearing down the device/
    // allocator above is still captured.
    if (m_debugMessenger != VK_NULL_HANDLE) {
        if (vkDestroyDebugUtilsMessengerEXT)
            vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
    m_physical = VK_NULL_HANDLE;
    m_graphicsQueue = VK_NULL_HANDLE;
    m_graphicsFamily = UINT32_MAX;
    m_info = ContextInfo{};
}

VkBool32 VKAPI_CALL VkContext::DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT type,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                                              void* userData) {
    (void)type;
    if (severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) {
        VkContext* ctx = static_cast<VkContext*>(userData);
        if (ctx) {
            ++ctx->m_validationMessageCount;
            ctx->m_lastValidationMessage =
                (data && data->pMessage) ? data->pMessage : "(no message)";
        }
    }
    // VK_FALSE: never abort the call that triggered validation.
    return VK_FALSE;
}

namespace {
VkContext* g_globalContext = nullptr;
} // namespace

void SetGlobalContext(VkContext* ctx) { g_globalContext = ctx; }
VkContext* GetGlobalContext() { return g_globalContext; }

} // namespace Onyx::Rendering
