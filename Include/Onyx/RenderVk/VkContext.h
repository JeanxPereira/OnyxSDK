#pragma once

// ── Include-order rule for all of Onyx::RenderVk ────────────────────────────
// volk owns Vulkan symbol resolution in this codebase: every function is
// called through a volk-loaded pointer, never through a loader import lib.
// That only works if VK_NO_PROTOTYPES is defined before <vulkan/vulkan.h> is
// first preprocessed anywhere in the translation unit -- volk.h does this
// itself (and the Onyx_RenderVk target also defines it explicitly, so the
// rule holds even for a TU that pulls in a Vulkan header before this one).
//
// Consequence: NEVER `#include <vulkan/vulkan.h>` directly, in this
// directory or anywhere that links Onyx::RenderVk. Always `#include <volk.h>`
// first (or exclusively) -- every later Vulkan header (vk_mem_alloc.h
// included) picks up volk's already-configured vulkan.h through its own
// `#include <vulkan/vulkan.h>` and the include guard makes that a no-op.
// This rule is binding for every RenderVk task after this one.
#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <string>

namespace Onyx::RenderVk {

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
    bool Init(bool presentSupport, std::string& err);

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

private:
    VkInstance       m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkDevice         m_device = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue = VK_NULL_HANDLE;
    uint32_t         m_graphicsFamily = UINT32_MAX;
    VmaAllocator     m_allocator = VK_NULL_HANDLE;
    ContextInfo      m_info;
};

} // namespace Onyx::RenderVk
