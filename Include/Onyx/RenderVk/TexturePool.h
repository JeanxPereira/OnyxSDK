#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace Onyx::RenderVk {

/// Generic N-frames-in-flight deferred-destruction bookkeeping (T10).
///
/// Extracted as its own pure class so it can be unit-tested (Tests/
/// rendervk_test.cpp) without a VkDevice: this file/class touches no
/// Vulkan type at all -- just frame-index arithmetic and a caller-supplied
/// `std::function<void()>` to run once retirement is safe. It exists
/// because the real reason to destroy a descriptor/image the moment a
/// viewer stops using it is unsafe: the GPU may still be reading it as
/// part of a frame that was already submitted (imgui_impl_vulkan's
/// descriptor set is referenced by whatever command buffer THIS frame's
/// ImGui_ImplVulkan_RenderDrawData recorded, which may not have finished
/// executing on the GPU yet) -- so the actual teardown must wait until at
/// least as many frames have elapsed as the swapchain keeps in flight.
///
/// Layer-placement note (T10 rider): the plan's file list names this class
/// `Onyx::RenderVk::TexturePool` outright, but everything past this pure
/// bookkeeping -- image upload via VkContext, ImGui_ImplVulkan_AddTexture/
/// RemoveTexture, the ImTextureID handed back to a viewer -- is an
/// imgui_impl_vulkan backend concern, and Onyx_RenderVk is deliberately
/// imgui-free (no backends/*.h include, no link dependency on imgui_lib --
/// see CMakeLists.txt's Onyx_RenderVk target comment). So the class here
/// stays exactly this: no VkContext, no VkImage, no ImGui. The real,
/// ImGui-touching `TexturePool` (which wraps one of these) lives one layer
/// up, at Include/Onyx/App/TexturePool.h -- see that file's own top
/// comment for why (and for why it is compiled into two different CMake
/// targets rather than one).
class DeferredDestroyQueue {
public:
    /// Schedules `destroy` to run once at least `framesInFlight` frames
    /// have elapsed since `retiredFrame` (i.e. once a later Collect() call
    /// sees `currentFrame >= retiredFrame + framesInFlight`). Never invokes
    /// `destroy` itself -- only Collect()/CollectAll() do that. Multiple
    /// entries may be retired on the same frame; they run in the order
    /// they were Retire()'d.
    void Retire(uint64_t retiredFrame, std::function<void()> destroy);

    /// Runs (and removes) every entry whose retirement is now safe, given
    /// `currentFrame` and `framesInFlight`. Safe to call every frame with a
    /// monotonically increasing `currentFrame` (the normal case); also
    /// safe to call with a `currentFrame` that never advances (nothing
    /// runs -- entries stay pending) or one that jumps forward by more
    /// than `framesInFlight` in a single call (every entry that now
    /// qualifies still runs; there is no per-call cap). `framesInFlight`
    /// of 0 means "safe as soon as Collect() is next called at all" (every
    /// pending entry with retiredFrame <= currentFrame runs).
    void Collect(uint64_t currentFrame, uint32_t framesInFlight);

    /// Runs every pending entry unconditionally, regardless of frame
    /// distance, and empties the queue -- for a shutdown path where the
    /// caller has already otherwise guaranteed (e.g. vkDeviceWaitIdle)
    /// that nothing on the GPU can still be reading any retired resource.
    void CollectAll();

    /// Entries currently awaiting Collect()/CollectAll() -- for tests and
    /// for a caller that wants to log/assert on pool churn.
    size_t PendingCount() const { return m_entries.size(); }

private:
    struct Entry {
        uint64_t              retiredFrame;
        std::function<void()> destroy;
    };

    std::vector<Entry> m_entries;
};

} // namespace Onyx::RenderVk
