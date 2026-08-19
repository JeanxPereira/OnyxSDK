#pragma once

// ── Include-order rule for all of Onyx::Rendering ────────────────────────────
// volk owns Vulkan symbol resolution in this codebase: every function is
// called through a volk-loaded pointer, never through a loader import lib.
// That only works if VK_NO_PROTOTYPES is defined before <vulkan/vulkan.h> is
// first preprocessed anywhere in the translation unit -- volk.h does this
// itself (and the Onyx_Render target also defines it explicitly, so the
// rule holds even for a TU that pulls in a Vulkan header before this one).
//
// Consequence: NEVER `#include <vulkan/vulkan.h>` directly, in this
// directory or anywhere that links Onyx::Rendering. Always `#include <volk.h>`
// first (or exclusively) -- every later Vulkan header (vk_mem_alloc.h
// included) picks up volk's already-configured vulkan.h through its own
// `#include <vulkan/vulkan.h>` and the include guard makes that a no-op.
// This rule is binding for every RenderVk task after this one.
#include <volk.h>
#include <vk_mem_alloc.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace Onyx::Rendering {

/// Snapshot of what VkContext::Init actually picked, for diagnostics and the
/// --vk-smoke report. Filled in only on a successful Init.
struct ContextInfo {
    std::string deviceName;
    uint32_t    apiVersion = 0;
    bool        validation = false;
};

/// Boots a Vulkan 1.3 instance + device + VMA allocator via volk. Headless by
/// default -- no VkSurfaceKHR is required or created here; a caller that
/// needs to present passes presentSupport=true so device/queue selection
/// also verifies platform presentation support and enables
/// VK_KHR_swapchain, but wiring an actual swapchain is a later task.
///
/// Device selection prefers a discrete GPU with a suitable graphics queue,
/// falling back to the first device that has one at all. Vulkan 1.3
/// dynamicRendering and synchronization2 are required (not merely
/// preferred): a device lacking either fails Init with a clear err rather
/// than booting a context later code can't render through.
class VkContext {
public:
    VkContext() = default;
    ~VkContext() { Shutdown(); }

    VkContext(const VkContext&) = delete;
    VkContext& operator=(const VkContext&) = delete;

    /// Returns false and fills err on any failure. Every object created
    /// before the failing step is torn down before returning -- a failed
    /// Init leaves nothing to Shutdown().
    ///
    /// extraInstanceExtensions: instance extensions the caller needs beyond
    /// what VkContext requests on its own (validation/debug-utils above,
    /// platform surface extensions below). Defaulted to empty so every
    /// existing headless caller (onyx-oracle, onyxbox-cli, the Tests
    /// target -- none of them pass presentSupport=true) is unaffected by
    /// this parameter's existence. A GUI caller that DOES pass
    /// presentSupport=true is the one that must supply
    /// glfwGetRequiredInstanceExtensions()'s result here -- VkContext
    /// itself must never link GLFW (see this header's include-order rule
    /// and every RenderVk task's binding "no GLFW in Onyx_Render" rule),
    /// so it cannot ask GLFW what a surface needs; the caller that already
    /// has a GLFWwindow is the only one that can. Entries are
    /// de-duplicated against VkContext's own list (and each other) before
    /// vkCreateInstance sees them -- requesting the same extension twice
    /// is a validation-layer complaint, not a hard error, but there is no
    /// reason to risk it.
    bool Init(bool presentSupport, std::string& err,
              const std::vector<const char*>& extraInstanceExtensions = {});

    /// Idempotent; safe to call on a context that never Init'd or already
    /// shut down.
    void Shutdown();

    VkInstance       Instance() const { return m_instance; }
    VkPhysicalDevice Physical() const { return m_physical; }
    VkDevice         Device() const { return m_device; }
    VkQueue          GraphicsQueue() const { return m_graphicsQueue; }
    uint32_t         GraphicsFamily() const { return m_graphicsFamily; }
    VmaAllocator     Allocator() const { return m_allocator; }
    const ContextInfo& Info() const { return m_info; }

    // ── Validation capture (T1-review addition) ─────────────────────────────
    // No debug-message sink existed when T1 landed; every later task needs
    // "zero validation messages" to be a checkable fact rather than
    // something eyeballed off stderr. When validation is active (Debug
    // builds, layer present) Init creates a VkDebugUtilsMessengerEXT scoped
    // to the instance -- warnings and errors increment the count below and
    // overwrite the last message. Both persist across Shutdown() (only Init
    // resets them) so a caller can inspect them right after tearing the
    // context down, catching messages raised during teardown itself.
    //
    // T4-review rider #1: the count is std::atomic<uint32_t> -- the
    // validation layer is free to invoke DebugCallback from a thread it
    // owns (not necessarily the thread that issued the Vulkan call being
    // validated), so a caller polling ValidationMessageCount() from
    // another thread must see a consistent value, not a torn read.
    // LastValidationMessage() is NOT similarly guarded: std::string has no
    // safe concurrent read/write, and only one thread reads it today
    // (right after Shutdown(), in every caller in this codebase so far).
    // TODO(later task): if a caller ever needs to read
    // LastValidationMessage() while validation is still active on another
    // thread, guard it (mutex or a second atomic<std::string>-shaped
    // scheme) -- documented-single-threaded is not a substitute for that
    // once such a caller exists.
    uint32_t         ValidationMessageCount() const { return m_validationMessageCount.load(); }
    const std::string& LastValidationMessage() const { return m_lastValidationMessage; }

private:
    static VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT type,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                                              void* userData);

    VkInstance       m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkDevice         m_device = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue = VK_NULL_HANDLE;
    uint32_t         m_graphicsFamily = UINT32_MAX;
    VmaAllocator     m_allocator = VK_NULL_HANDLE;
    ContextInfo      m_info;

    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    std::atomic<uint32_t> m_validationMessageCount{0};
    std::string      m_lastValidationMessage;
};

// ── Process-wide "reach the live context" accessor (T10) ────────────────
// The Shell (Window, T9) owns the one VkContext that actually presents;
// Window.h already exposes it to code that HOLDS a Window& (Window::
// vkContext()). What T10 adds is a second, narrower need: viewers
// (Viewport3D/ImageViewer/UiGallery, in Onyx_Shell) and VideoPlayer (in
// Onyx_Media, a target Onyx_Shell itself links -- see that target's own
// CMakeLists.txt comment for why it cannot link back to Onyx_Shell/
// Onyx::Api to reach a Window&) are constructed with no reference to the
// owning Window at all (Viewport3D's sole real construction site,
// DocumentBrowser.cpp, takes just a name -- matches every other viewer).
// Threading a VkContext& through every one of those constructors would
// touch call sites well outside this task's file list for zero benefit
// over the pattern this codebase already uses for the exact same problem
// (Onyx::Api's global facade over AppConfig/ViewerRegistry/DocumentWindow)
// -- this is that same idiom, just placed low enough (Onyx_Render, the
// one layer both Onyx_Shell and Onyx_Media already link) to be reachable
// from both without recreating Onyx::Api's link-cycle problem.
//
// A plain raw pointer, not a reference or shared_ptr: nothing here owns
// the VkContext (Window does, exclusively) and nothing here extends its
// lifetime -- a caller that reads GetGlobalContext() after Window has
// begun ~Window() (which clears this before m_vkContext is destroyed; see
// Window::exitVulkan()) gets nullptr, not a dangling pointer.
void       SetGlobalContext(VkContext* ctx);
VkContext* GetGlobalContext();

} // namespace Onyx::Rendering
