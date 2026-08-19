// Vulkan must be visible before GLFW's own header (pulled in transitively
// by <Onyx/App/Window.h> below) so GLFW's optional Vulkan support
// functions (glfwCreateWindowSurface, glfwGetPhysicalDevicePresentation-
// Support, ...) pick up the REAL VkInstance/VkPhysicalDevice/VkSurfaceKHR
// types instead of the void*-typedef fallback GLFW declares when it has
// never seen vulkan.h. This is a single-translation-unit ordering rule --
// see VkContext.h's "include-order rule" comment for the general form and
// why it binds every RenderVk-touching TU, including this Shell one that
// only forward-declares VkContext/RenderContext in its own header.
#include <volk.h>
#include <vk_mem_alloc.h>

#include <Onyx/App/Window.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "implot.h"

#include <Onyx/RenderVk/RenderContext.h>
#include <Onyx/RenderVk/VkContext.h>

#include <Onyx/Services/PathUtils.h>
#include <Onyx/Services/TaskManager.h>
#include <Onyx/Services/ThemeManager.h>
#include <Onyx/Services/Appearance.h>
#include <Onyx/Services/FrameScheduler.h>
#include "Services/ScaleManager.h"
#include "Fonts/FontManager.h"
#include <Onyx/Services/Events.h>
#include <Onyx/Services/Logger.h>
#include "App/NativeWindow.h"

namespace Onyx::App {

// -- Globals needed by GLFW callbacks -----------------------------------------
static Window* s_windowInstance = nullptr;

namespace {
constexpr uint32_t kFramesInFlight = 2;

void ImGuiVulkanCheckResult(VkResult err) {
    if (err == VK_SUCCESS) return;
    LOG_ERR("[Vulkan][imgui] backend call failed (VkResult %d)", static_cast<int>(err));
}
} // namespace

// -- VulkanState -- everything Window.cpp owns beyond the VkContext/
// RenderContext already forward-declared in Window.h. Nested inside Window
// (declared `struct VulkanState;` there) so it never needs a name outside
// this translation unit -- Vulkan swapchain/sync-object types stay entirely
// out of the public header per that file's top comment. ------------------
struct Window::VulkanState {
    // Surface + swapchain (recreated on resize/minimize-restore; the
    // surface itself is not).
    VkSurfaceKHR       surface            = VK_NULL_HANDLE;
    VkFormat           swapchainFormat    = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR    swapchainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D         swapchainExtent{0, 0};
    VkSwapchainKHR     swapchain          = VK_NULL_HANDLE;

    // Per-swapchain-image state (sized to the swapchain's own image count,
    // which need not equal kFramesInFlight).
    std::vector<VkImage>       images;
    std::vector<VkImageView>   imageViews;
    std::vector<VkImageLayout> imageLayouts;         // what THIS object left each image in
    std::vector<VkFence>       imagesInFlight;        // fence currently owning each image, or NULL
    std::vector<VkSemaphore>   renderFinishedSemaphores; // one per image (present waits on it)

    // Per-frame-in-flight state (fixed size kFramesInFlight; independent of
    // swapchain image count and NOT touched by a swapchain recreate).
    VkCommandPool   commandPools[kFramesInFlight]           = {};
    VkCommandBuffer commandBuffers[kFramesInFlight]         = {};
    VkSemaphore     imageAvailableSemaphores[kFramesInFlight] = {};
    VkFence         inFlightFences[kFramesInFlight]         = {};
    uint32_t        currentFrame = 0;

    bool framebufferResized = false;
};

static void glfw_error_callback(int error, const char* desc) {
    // GLFW_PLATFORM_ERROR (65544) "Cannot query workarea without screen" is a
    // known benign error that occurs on macOS when running under a remote
    // desktop session (TeamViewer, VNC, RDP, or any virtual display) instead
    // of a real physical NSScreen.  The Cocoa backend queries the monitor's
    // usable area during window creation/positioning; that call fails
    // gracefully on a virtual display -- the window still opens and renders
    // correctly, so the message is just noise.  All other errors are logged.
#if defined(__APPLE__)
    if (error == GLFW_PLATFORM_ERROR && desc && strstr(desc, "workarea"))
        return;
#endif
    fprintf(stderr, "GLFW error %d: %s\n", error, desc);
}

// -- Constructor / Destructor --------------------------------------------------

Window::Window()
    : m_configPath(PathUtils::resolvePath("onyx.toml"))
{
    s_windowInstance = this;

    // First thing in the process: every run writes its own dated file under
    // logs/, so the boot below is on record even if it is what goes wrong.
    // The global floor drops to Debug to feed the file; the on-screen panel
    // keeps its own Info floor and is unaffected.
    Onyx::Services::Log::SetMinLevel(Onyx::Services::Log::Level::Debug);
    m_sessionLog = Onyx::Services::SessionLog::Install();

    m_config = Onyx::Services::AppConfig::load(m_configPath);
    Onyx::Services::AppConfig::SetInstance(&m_config);

    // Smoke wiring (Task 2, M3b): subscribe once, for the life of the
    // Window, to prove the Workspace's EventBus actually reaches this
    // process end to end. Temporary-but-committed log lines -- superseded
    // once panels consume these events directly.
    m_onDocumentOpened = m_workspace.Events().On<Onyx::Modules::DocumentOpened>(
        [](const Onyx::Modules::DocumentOpened& ev) {
            LOG_INFO("[Workspace] document opened id=%llu",
                     (unsigned long long)ev.id);
        });
    m_onTreeReady = m_workspace.Events().On<Onyx::Modules::TreeReady>(
        [](const Onyx::Modules::TreeReady& ev) {
            LOG_INFO("[Workspace] tree ready id=%llu ok=%d",
                     (unsigned long long)ev.id, ev.ok ? 1 : 0);
        });

    initGLFW();
    initVulkan();
    initImGui();
    setupNativeWindow();

    // Initialize core systems
    Onyx::Services::TaskManager::init();
}

// -- run -- finalize App and enter the frame loop ------------------------------
// App init is deferred out of the constructor so the executable can inject the
// game panel/viewer registrar (via app()) before App::init() -- which is what
// invokes the registrar -- runs.

void Window::run() {
    m_app.init(m_window, &m_config, &m_workspace);

    // 1:1 ImHex: live resize via OS refresh callback
    glfwSetWindowRefreshCallback(m_window, [](GLFWwindow*) {
        if (s_windowInstance)
            s_windowInstance->fullFrame();
    });

    glfwSetDropCallback(m_window, [](GLFWwindow*, int, const char**) {});

    loop();
}

Window::~Window() {
    // Save config before shutdown
    {
        bool maximized = glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED);
        m_config.maximized = maximized;
        if (!maximized) {
            glfwGetWindowPos(m_window, &m_config.windowX, &m_config.windowY);
            glfwGetWindowSize(m_window, &m_config.windowW, &m_config.windowH);
        }

        // Panels no longer write appearance fields into the config; the owner
        // does, once, here. One less thing every slider has to remember.
        m_config.setAppearanceState(Onyx::Appearance::Get());

        m_config.save(m_configPath);
    }

    // Shutdown core systems
    EventShutdown::post();
    Onyx::Services::EventManager::clear();
    Onyx::Services::TaskManager::exit();

    // The last submitted frame's command buffer may still be executing on
    // the GPU right up to this point (presentFrame() only fences the CPU
    // side of frames-in-flight, it never waits for the final one). ImGui's
    // Vulkan backend destroys GPU resources (font texture, descriptor pool,
    // pipeline) that command buffer references, so exitImGui() is not safe
    // to call until the GPU has actually gone idle -- caught live: without
    // this wait, ImGui_ImplVulkan_Shutdown()'s vkDestroyPipeline() raised
    // VUID-vkDestroyPipeline-pipeline-00765 ("pipeline...currently in use
    // by VkCommandBuffer") on every run, an otherwise-invisible validation
    // error since it fires after the window is already gone.
    if (m_vkContext && m_vkContext->Device() != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_vkContext->Device());

    // exitVulkan() itself must finish (surface destroyed) BEFORE exitGLFW()
    // destroys the window whose native handle that surface was created
    // from.
    exitImGui();
    exitVulkan();
    exitGLFW();

    s_windowInstance = nullptr;

    // Last thing out: everything above still reached the file.
    Onyx::Services::SessionLog::Uninstall(m_sessionLog);
}

// -- initGLFW -----------------------------------------------------------------

void Window::initGLFW() {
    glfwSetErrorCallback(glfw_error_callback);

#if defined(__APPLE__)
    // Prevent the Cocoa backend from trying to install a global application
    // menu bar before a real screen is available.  On virtual/remote displays
    // (TeamViewer, VNC, headless CI) this avoids spurious platform errors
    // during glfwInit() and subsequent monitor queries.
    glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
#endif

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        std::exit(-1);
    }

    // No GL context: GLFW must not try to create one, or glfwCreateWindow
    // fails outright the moment a Vulkan-only driver is in play. Delegate
    // any remaining platform-specific hints (borderless decoration, etc.)
    // to configureGLFW() -- GL version/profile hints used to live there;
    // they are gone now, this is the one hint every platform needs.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    configureGLFW();

    m_window = glfwCreateWindow(m_config.windowW, m_config.windowH,
                                m_config.windowTitle.c_str(), nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        fprintf(stderr, "Failed to create GLFW window\n");
        std::exit(-1);
    }

    glfwSetWindowPos(m_window, m_config.windowX, m_config.windowY);
    if (m_config.maximized)
        glfwMaximizeWindow(m_window);

    // Store 'this' for callbacks -- set before any callback below (or
    // initVulkan()'s own framebuffer-resize callback) can possibly fire.
    glfwSetWindowUserPointer(m_window, this);

    // Track window attributes dynamically since macOS shutdown can miss late bounds queries
    glfwSetWindowPosCallback(m_window, [](GLFWwindow* window, int xpos, int ypos) {
        if (!glfwGetWindowAttrib(window, GLFW_MAXIMIZED)) {
            if (Window* w = (Window*)glfwGetWindowUserPointer(window)) {
                w->m_config.windowX = xpos;
                w->m_config.windowY = ypos;
            }
        }
    });

    glfwSetWindowSizeCallback(m_window, [](GLFWwindow* window, int width, int height) {
        if (!glfwGetWindowAttrib(window, GLFW_MAXIMIZED)) {
            if (Window* w = (Window*)glfwGetWindowUserPointer(window)) {
                w->m_config.windowW = width;
                w->m_config.windowH = height;
            }
        }
    });
}

// -- initVulkan -----------------------------------------------------------------
// VkContext (presentSupport=true) + a glfwCreateWindowSurface-made surface +
// the swapchain + the frames-in-flight sync objects. Runs after initGLFW()
// (needs m_window) and before initImGui() (needs the swapchain's image
// count/format to configure imgui_impl_vulkan's dynamic-rendering pipeline).

void Window::initVulkan() {
    m_vk = std::make_unique<VulkanState>();
    m_vkContext = std::make_unique<Onyx::RenderVk::VkContext>();
    m_renderContext = std::make_unique<Onyx::RenderVk::RenderContext>();

    std::string err;
    if (!m_vkContext->Init(/*presentSupport=*/true, err)) {
        fprintf(stderr, "Failed to initialize Vulkan context: %s\n", err.c_str());
        LOG_ERR("[Vulkan] VkContext::Init failed: %s", err.c_str());
        std::exit(-1);
    }
    LOG_INFO("[Vulkan] device=\"%s\" apiVersion=%u.%u.%u validation=%d",
             m_vkContext->Info().deviceName.c_str(),
             VK_API_VERSION_MAJOR(m_vkContext->Info().apiVersion),
             VK_API_VERSION_MINOR(m_vkContext->Info().apiVersion),
             VK_API_VERSION_PATCH(m_vkContext->Info().apiVersion),
             m_vkContext->Info().validation ? 1 : 0);

    VkResult vr = glfwCreateWindowSurface(m_vkContext->Instance(), m_window, nullptr, &m_vk->surface);
    if (vr != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan surface (VkResult %d)\n", static_cast<int>(vr));
        LOG_ERR("[Vulkan] glfwCreateWindowSurface failed (VkResult %d)", static_cast<int>(vr));
        std::exit(-1);
    }

    // VkContext already verified (Windows: vkGetPhysicalDeviceWin32Presentation
    // SupportKHR) that the chosen graphics family can present to THIS
    // platform's windowing system before a real surface existed. Checking
    // again against the actual surface is cheap and catches the
    // (theoretical, shouldn't-happen-given-the-above) case where a driver
    // disagrees once a concrete surface is involved -- logged, not fatal,
    // since every platform this milestone targets picks the same family
    // either way.
    VkBool32 presentSupported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_vkContext->Physical(), m_vkContext->GraphicsFamily(),
                                          m_vk->surface, &presentSupported);
    if (!presentSupported) {
        LOG_ERR("[Vulkan] chosen graphics queue family cannot present to the real surface "
                 "(VkContext's platform pre-check said it could)");
    }

    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(m_window, &fbW, &fbH);
    createSwapchain(static_cast<uint32_t>(fbW), static_cast<uint32_t>(fbH));
    createFrameSync();

    // Framebuffer-resize callback: separate from the window-size callback
    // above (that one persists screen-coordinate size into m_config; this
    // one reacts to the pixel-resolution change a swapchain actually cares
    // about, which on a HiDPI display is not the same number).
    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* window, int, int) {
        if (Window* w = (Window*)glfwGetWindowUserPointer(window))
            w->m_vk->framebufferResized = true;
    });

    LOG_INFO("[Vulkan] swapchain init: %ux%u, %zu image(s), format=%d, FIFO present, "
             "%u frame(s) in flight",
             m_vk->swapchainExtent.width, m_vk->swapchainExtent.height, m_vk->images.size(),
             static_cast<int>(m_vk->swapchainFormat), kFramesInFlight);
}

// -- createSwapchain / destroySwapchain / recreateSwapchain --------------------

void Window::createSwapchain(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        // Minimized (or not yet laid out): leave swapchain == VK_NULL_HANDLE.
        // frameEnd()'s present step checks the framebuffer size itself every
        // frame and simply skips the GPU frame while it stays 0x0 -- see
        // that method's doc comment.
        m_vk->swapchainExtent = {0, 0};
        return;
    }

    Onyx::RenderVk::VkContext& ctx = *m_vkContext;

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.Physical(), m_vk->surface, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.Physical(), m_vk->surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (formatCount > 0)
        vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.Physical(), m_vk->surface, &formatCount, formats.data());

    // RGBA8/BGRA8 as available (plan's own wording): prefer plain RGBA8
    // UNORM (matches Pipelines.h's kColorFormat, so a future offscreen-
    // target blit/compare against the swapchain never has to think about a
    // channel swap), fall back to BGRA8 UNORM (what most desktop drivers
    // actually report first), else whatever the surface listed first.
    VkSurfaceFormatKHR chosen = formats.empty()
        ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
        : formats[0];
    bool haveExact = false;
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM) { chosen = f; haveExact = true; break; }
    }
    if (!haveExact) {
        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM) { chosen = f; break; }
        }
    }

    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent.width  = std::min(std::max(width,  caps.minImageExtent.width),  caps.maxImageExtent.width);
        extent.height = std::min(std::max(height, caps.minImageExtent.height), caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) {
        m_vk->swapchainExtent = {0, 0};
        return;
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR info{};
    info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface           = m_vk->surface;
    info.minImageCount     = imageCount;
    info.imageFormat       = chosen.format;
    info.imageColorSpace    = chosen.colorSpace;
    info.imageExtent       = extent;
    info.imageArrayLayers  = 1;
    info.imageUsage        = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    // Single graphics+present queue family (VkContext::Init selected it for
    // exactly that combination) -- no queue family transfer needed.
    info.imageSharingMode  = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform      = caps.currentTransform;
    info.compositeAlpha    = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode       = VK_PRESENT_MODE_FIFO_KHR; // guaranteed available; plan pins FIFO
    info.clipped           = VK_TRUE;
    info.oldSwapchain      = VK_NULL_HANDLE; // destroySwapchain() already ran (see recreateSwapchain)

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    VkResult vr = vkCreateSwapchainKHR(ctx.Device(), &info, nullptr, &newSwapchain);
    if (vr != VK_SUCCESS) {
        fprintf(stderr, "vkCreateSwapchainKHR failed (VkResult %d)\n", static_cast<int>(vr));
        LOG_ERR("[Vulkan] vkCreateSwapchainKHR failed (VkResult %d)", static_cast<int>(vr));
        std::exit(-1);
    }

    m_vk->swapchain            = newSwapchain;
    m_vk->swapchainFormat      = chosen.format;
    m_vk->swapchainColorSpace  = chosen.colorSpace;
    m_vk->swapchainExtent      = extent;

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(ctx.Device(), m_vk->swapchain, &actualCount, nullptr);
    m_vk->images.resize(actualCount);
    vkGetSwapchainImagesKHR(ctx.Device(), m_vk->swapchain, &actualCount, m_vk->images.data());

    m_vk->imageViews.resize(actualCount);
    m_vk->imageLayouts.assign(actualCount, VK_IMAGE_LAYOUT_UNDEFINED);
    m_vk->imagesInFlight.assign(actualCount, VK_NULL_HANDLE);
    m_vk->renderFinishedSemaphores.resize(actualCount);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < actualCount; ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                            = m_vk->images[i];
        viewInfo.viewType                         = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                           = m_vk->swapchainFormat;
        viewInfo.components                       = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                                       VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        viewInfo.subresourceRange.aspectMask       = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel     = 0;
        viewInfo.subresourceRange.levelCount       = 1;
        viewInfo.subresourceRange.baseArrayLayer   = 0;
        viewInfo.subresourceRange.layerCount       = 1;

        vr = vkCreateImageView(ctx.Device(), &viewInfo, nullptr, &m_vk->imageViews[i]);
        if (vr != VK_SUCCESS) {
            fprintf(stderr, "vkCreateImageView failed for swapchain image %u (VkResult %d)\n", i,
                    static_cast<int>(vr));
            LOG_ERR("[Vulkan] vkCreateImageView failed for swapchain image %u (VkResult %d)", i,
                    static_cast<int>(vr));
            std::exit(-1);
        }

        vr = vkCreateSemaphore(ctx.Device(), &semInfo, nullptr, &m_vk->renderFinishedSemaphores[i]);
        if (vr != VK_SUCCESS) {
            fprintf(stderr, "vkCreateSemaphore (renderFinished[%u]) failed (VkResult %d)\n", i,
                    static_cast<int>(vr));
            LOG_ERR("[Vulkan] vkCreateSemaphore (renderFinished[%u]) failed (VkResult %d)", i,
                    static_cast<int>(vr));
            std::exit(-1);
        }
    }

    // ImGui's own MinImageCount bookkeeping (only meaningful once
    // initImGui() has already run -- a mid-run resize, not the first
    // creation from the constructor).
    if (ImGui::GetCurrentContext())
        ImGui_ImplVulkan_SetMinImageCount(std::max(caps.minImageCount, 2u));
}

void Window::destroySwapchain() {
    if (!m_vk) return;
    VkDevice device = m_vkContext->Device();

    for (VkSemaphore s : m_vk->renderFinishedSemaphores)
        if (s) vkDestroySemaphore(device, s, nullptr);
    m_vk->renderFinishedSemaphores.clear();

    for (VkImageView v : m_vk->imageViews)
        if (v) vkDestroyImageView(device, v, nullptr);
    m_vk->imageViews.clear();

    m_vk->images.clear();
    m_vk->imageLayouts.clear();
    m_vk->imagesInFlight.clear();

    if (m_vk->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, m_vk->swapchain, nullptr);
        m_vk->swapchain = VK_NULL_HANDLE;
    }
}

void Window::recreateSwapchain() {
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(m_window, &fbW, &fbH);

    // vkDeviceWaitIdle before tearing down the old swapchain's images: the
    // simplest correct option (a resize/restore is not a hot path), and it
    // sidesteps every subtlety of VkSwapchainCreateInfoKHR::oldSwapchain
    // retirement (still-in-flight presents against the old images, etc.).
    vkDeviceWaitIdle(m_vkContext->Device());
    destroySwapchain();
    createSwapchain(static_cast<uint32_t>(fbW), static_cast<uint32_t>(fbH));
    m_vk->framebufferResized = false;
}

// -- createFrameSync / destroyFrameSync -----------------------------------------
// Frames-in-flight resources: fixed at kFramesInFlight, created once here
// and never touched by a swapchain recreate (see the field comments on
// VulkanState above).

void Window::createFrameSync() {
    VkDevice device = m_vkContext->Device();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_vkContext->GraphicsFamily();

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // first wait must not block forever

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        VkResult vr = vkCreateCommandPool(device, &poolInfo, nullptr, &m_vk->commandPools[i]);
        if (vr != VK_SUCCESS) {
            fprintf(stderr, "vkCreateCommandPool failed (VkResult %d)\n", static_cast<int>(vr));
            LOG_ERR("[Vulkan] vkCreateCommandPool failed (VkResult %d)", static_cast<int>(vr));
            std::exit(-1);
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = m_vk->commandPools[i];
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vr = vkAllocateCommandBuffers(device, &allocInfo, &m_vk->commandBuffers[i]);
        if (vr != VK_SUCCESS) {
            fprintf(stderr, "vkAllocateCommandBuffers failed (VkResult %d)\n", static_cast<int>(vr));
            LOG_ERR("[Vulkan] vkAllocateCommandBuffers failed (VkResult %d)", static_cast<int>(vr));
            std::exit(-1);
        }

        vr = vkCreateSemaphore(device, &semInfo, nullptr, &m_vk->imageAvailableSemaphores[i]);
        if (vr != VK_SUCCESS) {
            fprintf(stderr, "vkCreateSemaphore (imageAvailable[%u]) failed (VkResult %d)\n", i,
                    static_cast<int>(vr));
            LOG_ERR("[Vulkan] vkCreateSemaphore (imageAvailable[%u]) failed (VkResult %d)", i,
                    static_cast<int>(vr));
            std::exit(-1);
        }

        vr = vkCreateFence(device, &fenceInfo, nullptr, &m_vk->inFlightFences[i]);
        if (vr != VK_SUCCESS) {
            fprintf(stderr, "vkCreateFence failed (VkResult %d)\n", static_cast<int>(vr));
            LOG_ERR("[Vulkan] vkCreateFence failed (VkResult %d)", static_cast<int>(vr));
            std::exit(-1);
        }
    }
}

void Window::destroyFrameSync() {
    if (!m_vk || !m_vkContext) return;
    VkDevice device = m_vkContext->Device();
    if (device == VK_NULL_HANDLE) return;

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (m_vk->inFlightFences[i]) vkDestroyFence(device, m_vk->inFlightFences[i], nullptr);
        if (m_vk->imageAvailableSemaphores[i]) vkDestroySemaphore(device, m_vk->imageAvailableSemaphores[i], nullptr);
        if (m_vk->commandPools[i]) vkDestroyCommandPool(device, m_vk->commandPools[i], nullptr); // frees its buffer too
    }
}

// -- initImGui ----------------------------------------------------------------

void Window::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    m_imguiIniPath = PathUtils::resolvePath("imgui.ini");
    io.IniFilename = m_imguiIniPath.c_str(); // ImGui owns its native ini (auto-loads/saves)
    io.IniSavingRate = 2.0f;

    // Apply centralized accent-derived theme (replaces StyleColorsDark + applyAccent)
    Onyx::Theme::ApplyTheme(ImVec4(m_config.accentR, m_config.accentG, m_config.accentB, m_config.accentA),
                           (Onyx::Theme::ThemeMode)m_config.themeMode);


    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::GetStyle().WindowRounding = 0.0f;
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Initialize centralized scale system with OS native DPI
    float nativeScale = 1.0f;
#if defined(__APPLE__)
    {
        float xscale = 1.0f, yscale = 1.0f;
        glfwGetWindowContentScale(m_window, &xscale, &yscale);
        nativeScale = xscale;
    }
#endif
    Onyx::Scale::Init(m_config.uiScale, nativeScale);

    // Seed the appearance owner from config. The font half of the environment
    // is filled in later (App::init populates the font list first); the first
    // Commit() runs at the end of frame 1, by which time everything is ready.
    {
        Onyx::Appearance::Environment env;
        env.nativeScale       = nativeScale;
        env.systemPrefersDark = (Onyx::Theme::GetEffectiveMode() != Onyx::Theme::ThemeMode::Light);
        Onyx::Appearance::SetEnvironment(env);

        Onyx::Appearance::Set(m_config.appearanceState());

        // Apply now so frame 1 already has the house metrics. The font list is
        // not populated yet, so this commit skips the atlas; App::init fills in
        // the default font path and the next commit picks it up.
        Onyx::Appearance::Commit();
    }

    ImGui_ImplGlfw_InitForVulkan(m_window, true);

    Onyx::RenderVk::VkContext& ctx = *m_vkContext;

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion       = ctx.Info().apiVersion;
    initInfo.Instance         = ctx.Instance();
    initInfo.PhysicalDevice   = ctx.Physical();
    initInfo.Device           = ctx.Device();
    initInfo.QueueFamily      = ctx.GraphicsFamily();
    initInfo.Queue            = ctx.GraphicsQueue();
    initInfo.DescriptorPool   = VK_NULL_HANDLE;
    // Backend auto-creates its own pool (VK_DESCRIPTOR_POOL_CREATE_FREE_
    // DESCRIPTOR_SET_BIT, per imgui_impl_vulkan.h's "About descriptor pool"
    // note) when DescriptorPoolSize > 0. 64 leaves headroom over the font
    // atlas's own handful of descriptors for T10's per-viewer
    // ImGui_ImplVulkan_AddTexture() churn.
    initInfo.DescriptorPoolSize = 64;
    initInfo.MinImageCount    = std::max<uint32_t>(kFramesInFlight, 2);
    initInfo.ImageCount       = static_cast<uint32_t>(m_vk->images.size());
    initInfo.PipelineCache    = VK_NULL_HANDLE;
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_vk->swapchainFormat;
    // Secondary OS windows (floating/undocked panels, ImGuiConfigFlags_
    // ViewportsEnable is on above) are created and driven entirely by the
    // backend itself -- mirror the main window's dynamic-rendering setup so
    // those get the same pipeline shape instead of falling back to a
    // (nonexistent, since this app never creates one) VkRenderPass path.
    initInfo.PipelineInfoForViewports = initInfo.PipelineInfoMain;
    initInfo.Allocator        = nullptr;
    initInfo.CheckVkResultFn  = &ImGuiVulkanCheckResult;

    ImGui_ImplVulkan_Init(&initInfo);
}

// -- exitImGui / exitGLFW / exitVulkan ------------------------------------------

void Window::exitImGui() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}

void Window::exitVulkan() {
    if (!m_vkContext) return;
    VkDevice device = m_vkContext->Device();
    if (device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(device);

    destroySwapchain();
    destroyFrameSync();

    if (m_vk && m_vk->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_vkContext->Instance(), m_vk->surface, nullptr);
        m_vk->surface = VK_NULL_HANDLE;
    }

    // Snapshot before Shutdown(): ValidationMessageCount()/
    // LastValidationMessage() persist across it (VkContext.h's own
    // guarantee) specifically so a caller can still read what Shutdown()
    // itself raised, so read AFTER Shutdown() rather than before -- this is
    // the log line the T9 proof asserts "zero validation errors" against.
    m_vkContext->Shutdown();
    const uint32_t validationCount = m_vkContext->ValidationMessageCount();
    if (validationCount != 0) {
        LOG_ERR("[Vulkan] shutdown: %u validation message(s); last: %s", validationCount,
                 m_vkContext->LastValidationMessage().c_str());
    } else {
        LOG_INFO("[Vulkan] shutdown: %u validation message(s)", validationCount);
    }

    m_renderContext.reset();
    m_vkContext.reset();
    m_vk.reset();
}

void Window::exitGLFW() {
    glfwDestroyWindow(m_window);
    glfwTerminate();
    m_window = nullptr;
}

// -- loop ---------------------------------------------------------------------

void Window::loop() {
    while (!glfwWindowShouldClose(m_window)) {
        // Smart event handling: poll aggressively when active, idle otherwise
        if (m_firstFrame) {
            Onyx::Frame::BeginFrame(glfwGetTime());
            glfwPollEvents();
            m_firstFrame = false;
        } else {
            // Time-driven work (colour transitions, progress, playback) has
            // no input to infer activity from, so it asks explicitly.
            const bool animating = Onyx::Frame::BeginFrame(glfwGetTime());
            // Input keeps the loop hot while the user is actually doing
            // something; everything else now comes through the scheduler.
            //
            // Note what is NOT here any more: "any window is a separate OS
            // viewport". That pinned the app at full speed forever the moment a
            // panel was undocked -- measured at ~180 fps with nothing on screen
            // moving. Dragging a floating window is already covered by the
            // mouse tests, and viewports are rendered every frame regardless.
            const bool active = animating
                       || ImGui::IsMouseDown(ImGuiMouseButton_Left)
                       || ImGui::IsMouseDown(ImGuiMouseButton_Right)
                       || ImGui::IsMouseDown(ImGuiMouseButton_Middle)
                       || ImGui::IsAnyItemActive()
                       || ImGui::IsKeyDown(ImGuiMod_Ctrl);

#if defined(__APPLE__)
            if (NativeWindow::macosIsWindowBeingResized(m_window))
                Onyx::Frame::RequestRedraw();
#endif

            if (active) {
                glfwPollEvents();
            } else {
                glfwWaitEventsTimeout(1.0 / 15.0); // ~15 FPS idle (down from 30)
            }
        }

#if defined(__APPLE__)
        // Suppress hover effects during live resize (ImHex pattern)
        if (NativeWindow::macosIsWindowBeingResized(m_window))
            ImGui::GetIO().MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
#endif

        fullFrame();

        if (m_app.wantClose())
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
    }
}

// -- fullFrame -- crash-protected frame ---------------------------------------

void Window::fullFrame() {
    if (!m_window) return;

    static uint32_t crashWatchdog = 0;

#if !defined(DEBUG) && !defined(_DEBUG)
    try {
#endif
        frameBegin();
        frame();
        frameEnd();
#if !defined(DEBUG) && !defined(_DEBUG)
        crashWatchdog = 0;
    } catch (...) {
        if (++crashWatchdog > 10) std::abort();
        ImGui::EndFrame();
        fprintf(stderr, "Exception caught in fullFrame(), watchdog=%u\n", crashWatchdog);
    }
#endif
}

// -- frameBegin ---------------------------------------------------------------

void Window::frameBegin() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Drive the palette transition (preset switches, ease-out). Inside the
    // frame: it only writes colours, unlike Commit which may rebake the atlas.
    Onyx::Appearance::Tick();

    beginNativeWindowFrame();
}

// -- frame --------------------------------------------------------------------

void Window::frame() {
    // Shell pumps the Workspace once per frame, main thread only (Task 2,
    // M3b). Jobs() runs any finished async parse's Done callback (which
    // flips Document::ready and posts through Events()); pumping Jobs
    // first means a job that finished this frame gets its event dispatched
    // this same frame instead of one frame late.
    m_workspace.Jobs().Pump();
    m_workspace.Events().Pump();

    m_app.frame();
}

// -- frameEnd -----------------------------------------------------------------
// Per-frame Vulkan record/submit/present (T9). Order, per the plan: acquire
// -> record (RenderContext's raw-floor passes, then the ImGui pass, both
// inside ONE dynamic-rendering scope over the swapchain image) -> present.
// A viewer's own scene draw (into ITS OWN OffscreenTarget, e.g. Viewport3D
// from T10 on) happens earlier, during m_app.frameEnd()'s ImGui calls above
// in frame()/frameBegin() -- by the time this method's vkCmdBeginRendering
// runs, ImGui::Render() has already produced this frame's whole draw list
// and every panel has already recorded (or, pre-T10, simply not yet
// recorded) into its own resources.

void Window::frameEnd() {
    endNativeWindowFrame();

    ImGui::Render();

    // Minimized (or not yet laid out): skip the GPU frame entirely rather
    // than blocking on it (the plan's own "0x0 = skip frame"). ImGui::Render()
    // above still ran, so the next NewFrame() picks up cleanly; nothing here
    // touches the swapchain, which may not even exist yet.
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(m_window, &fbW, &fbH);
    if (fbW == 0 || fbH == 0) {
        m_vk->framebufferResized = true; // force a fresh swapchain once restored
    } else {
        if (m_vk->swapchain == VK_NULL_HANDLE || m_vk->framebufferResized)
            recreateSwapchain();

        if (m_vk->swapchain != VK_NULL_HANDLE)
            presentFrame();
    }

    // Viewport windows (external OS windows): imgui_impl_vulkan manages
    // their own swapchains/pipelines internally once ViewportsEnable is on
    // and Init() saw UseDynamicRendering -- no GL-style "make context
    // current" dance needed here, just the same two calls GL used.
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    // Font rebuild MUST happen after all rendering is complete.
    // Rebuilding the atlas before Render() invalidates the font texture
    // that the current frame's draw commands reference.
    // One owner, one place, once per frame, outside the frame: Commit applies
    // any pending appearance change and uploads a rebuilt atlas. A frame with
    // nothing pending costs a single state comparison.
    Onyx::Appearance::Commit();

    if (Onyx::Fonts::IsPendingRebuild()) {
        Onyx::Fonts::UploadAtlas();
    }

    m_app.frameEnd();
}

// -- presentFrame -- acquire/record/submit/present for one swapchain image ----
// Split out of frameEnd() only for readability; always called with a valid
// (non-VK_NULL_HANDLE) m_vk->swapchain and a non-zero framebuffer.

void Window::presentFrame() {
    VulkanState& vk = *m_vk;
    VkDevice device = m_vkContext->Device();

    vkWaitForFences(device, 1, &vk.inFlightFences[vk.currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult vr = vkAcquireNextImageKHR(device, vk.swapchain, UINT64_MAX,
                                         vk.imageAvailableSemaphores[vk.currentFrame],
                                         VK_NULL_HANDLE, &imageIndex);
    if (vr == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return; // try again next frame
    }
    if (vr != VK_SUCCESS && vr != VK_SUBOPTIMAL_KHR) {
        LOG_ERR("[Vulkan] vkAcquireNextImageKHR failed (VkResult %d)", static_cast<int>(vr));
        return;
    }

    // If this swapchain image is still being read by a previous frame-in-
    // flight slot's submission (imageCount > kFramesInFlight makes that
    // possible), wait for that slot's fence before touching it again.
    if (vk.imagesInFlight[imageIndex] != VK_NULL_HANDLE)
        vkWaitForFences(device, 1, &vk.imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    vk.imagesInFlight[imageIndex] = vk.inFlightFences[vk.currentFrame];

    vkResetFences(device, 1, &vk.inFlightFences[vk.currentFrame]);

    VkCommandBuffer cmd = vk.commandBuffers[vk.currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Barrier: whatever this object left the image in (UNDEFINED on its
    // very first use, PRESENT_SRC_KHR on every frame after) -> COLOR_
    // ATTACHMENT_OPTIMAL, sync2 throughout per the plan.
    {
        VkImageMemoryBarrier2 b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        b.srcAccessMask       = VK_ACCESS_2_NONE;
        b.dstStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        b.dstAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        b.oldLayout           = vk.imageLayouts[imageIndex];
        b.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = vk.images[imageIndex];
        b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo dep{};
        dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers    = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
        vk.imageLayouts[imageIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView   = vk.imageViews[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.10f, 0.10f, 0.10f, 1.0f}};

    VkRenderingInfo renderInfo{};
    renderInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea.offset    = {0, 0};
    renderInfo.renderArea.extent    = vk.swapchainExtent;
    renderInfo.layerCount           = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments    = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderInfo);

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(vk.swapchainExtent.width),
                        static_cast<float>(vk.swapchainExtent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, vk.swapchainExtent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Raw-floor passes (T8's RenderContext) run first, still inside this
    // same dynamic-rendering scope -- a consuming app that wants to draw
    // directly onto the swapchain image gets exactly one hook, right here,
    // before ImGui's own overlay. Nothing registers a pass yet in T9; this
    // wires the mechanism T10+ (and any raw-floor consumer) draws through.
    Onyx::RenderVk::FrameHandles handles{device, m_vkContext->GraphicsQueue(), cmd,
                                          m_vkContext->GraphicsFamily(), m_vkContext->Allocator()};
    m_renderContext->Execute(handles);

    if (ImDrawData* drawData = ImGui::GetDrawData())
        ImGui_ImplVulkan_RenderDrawData(drawData, cmd);

    vkCmdEndRendering(cmd);

    // Barrier: COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR.
    {
        VkImageMemoryBarrier2 b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        b.srcAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        b.dstStageMask        = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        b.dstAccessMask       = VK_ACCESS_2_NONE;
        b.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = vk.images[imageIndex];
        b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo dep{};
        dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers    = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
        vk.imageLayouts[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    vkEndCommandBuffer(cmd);

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = vk.imageAvailableSemaphores[vk.currentFrame];
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = vk.renderFinishedSemaphores[imageIndex];
    // ALL_COMMANDS rather than COLOR_ATTACHMENT_OUTPUT: the final layout-
    // transition barrier above has dstStage=BOTTOM_OF_PIPE (nothing on the
    // GPU reads/writes the image again this submission), so the semaphore
    // signal must wait for the whole submission, not just the color-write
    // stage that barrier's own src side depends on.
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmd;

    VkSubmitInfo2 submit{};
    submit.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount   = 1;
    submit.pWaitSemaphoreInfos      = &waitInfo;
    submit.commandBufferInfoCount   = 1;
    submit.pCommandBufferInfos      = &cmdInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos    = &signalInfo;

    vr = vkQueueSubmit2(m_vkContext->GraphicsQueue(), 1, &submit, vk.inFlightFences[vk.currentFrame]);
    if (vr != VK_SUCCESS)
        LOG_ERR("[Vulkan] vkQueueSubmit2 failed (VkResult %d)", static_cast<int>(vr));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &vk.renderFinishedSemaphores[imageIndex];
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &vk.swapchain;
    presentInfo.pImageIndices      = &imageIndex;

    vr = vkQueuePresentKHR(m_vkContext->GraphicsQueue(), &presentInfo);
    if (vr == VK_ERROR_OUT_OF_DATE_KHR || vr == VK_SUBOPTIMAL_KHR || vk.framebufferResized) {
        recreateSwapchain();
    } else if (vr != VK_SUCCESS) {
        LOG_ERR("[Vulkan] vkQueuePresentKHR failed (VkResult %d)", static_cast<int>(vr));
    }

    vk.currentFrame = (vk.currentFrame + 1) % kFramesInFlight;
}

} // namespace Onyx::App
