#pragma once

// See Onyx/RenderVk/VkContext.h's include-order rule: volk.h, then
// vk_mem_alloc.h, before any other Vulkan-touching header. VkResources.h
// (pulled in below, via VkContext.h) already honors that, so this file
// does not need to repeat the pair itself.
#include <Onyx/RenderVk/TexturePool.h> // Onyx::RenderVk::DeferredDestroyQueue
#include <Onyx/RenderVk/VkResources.h> // Onyx::RenderVk::Image2D

#include <imgui.h>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Onyx::RenderVk { class VkContext; }

namespace Onyx::App {

// ═══════════════════════════════════════════════════════════════════════
// Layer-placement note (T10 rider): the M4 plan's file list names this
// class `Onyx::RenderVk::TexturePool` (Include/Onyx/RenderVk/TexturePool.
// {h,cpp}) outright. It does not live there. Everything this class
// actually does past the raw image upload -- ImGui_ImplVulkan_AddTexture/
// RemoveTexture, handing back a plain ImTextureID a viewer passes straight
// to ImGui::Image() -- is an imgui_impl_vulkan backend concern, and
// Onyx_RenderVk is deliberately imgui-free: no backends/*.h include, no
// link dependency on imgui_lib (see CMakeLists.txt's Onyx_RenderVk target
// comment, which predates this task and already states that constraint
// for T9's own Window.cpp). Putting an ImGui-touching class inside
// Onyx_RenderVk would either violate that (if the .cpp really includes
// imgui_impl_vulkan.h) or silently not compile (Onyx_RenderVk has no
// imgui include path at all).
//
// So the class is split in two, one layer apart:
//   - Onyx::RenderVk::DeferredDestroyQueue (Include/Onyx/RenderVk/
//     TexturePool.h) is the pure, Vulkan-and-ImGui-free N-frames-in-flight
//     bookkeeping -- pure-tested in Tests/rendervk_test.cpp with no
//     device, per the plan's own testing note.
//   - This class wraps one of those, adds the actual VkImage upload
//     (Onyx::RenderVk::Resources) and the actual ImGui_ImplVulkan_*
//     registration, and lives in the Shell layer instead -- specifically
//     Onyx_Shell AND Onyx_Media both (see this file's .cpp for why it is
//     compiled twice rather than once).
// ═══════════════════════════════════════════════════════════════════════
//
// Ownership/ lifetime: one instance per viewer that needs textures
// (Viewport3D, ImageViewer, VideoPlayer each own exactly one). Nothing is
// shared between instances -- the only truly process-wide resource in
// play is imgui_impl_vulkan's own descriptor pool (T9's
// DescriptorPoolSize=64), which every instance's AddTexture call draws
// from regardless of which TexturePool made the call, so per-viewer
// ownership costs nothing beyond that shared budget.
//
// Frame-latency-safe destruction: Remove() (and the implicit remove
// RegisterExternalView() does for a caller-supplied `oldId`) never
// destroys anything itself -- both the backing VkImage/VkImageView (owned
// entries only) and the imgui descriptor set (ImGui_ImplVulkan_
// RemoveTexture) stay alive and valid until AdvanceFrame() has walked at
// least kFramesInFlight frames past the call. This matters because the
// SAME frame that calls Remove() may already have recorded an
// ImGui::Image() draw command referencing the old ImTextureID earlier in
// that frame -- imgui_impl_vulkan's RenderDrawData for this frame (and any
// frame still in flight on the GPU) must still find a live, valid
// descriptor when it runs.
class TexturePool {
public:
    explicit TexturePool(Onyx::RenderVk::VkContext& ctx, uint32_t framesInFlight = 2);
    ~TexturePool();

    TexturePool(const TexturePool&) = delete;
    TexturePool& operator=(const TexturePool&) = delete;

    /// Uploads `rgba` (tightly packed, width*height*4 bytes, R8G8B8A8) as a
    /// new SAMPLED image this pool owns, registers it with
    /// imgui_impl_vulkan, and returns an ImTextureID ready for
    /// ImGui::Image()/ImDrawList::AddImage() this same frame. Returns
    /// ImTextureID_Invalid (0) and fills `err` on any failure (nothing is
    /// left half-created to Remove() manually).
    ImTextureID Create(uint32_t width, uint32_t height, const void* rgba, std::string& err);

    /// Re-uploads new RGBA content into the SAME image/descriptor a live
    /// Create() call returned -- added beyond the plan's own wording for
    /// VideoPlayer's per-frame updates (disclosed in task-10-report.md).
    /// `width`/`height` must equal the image's own (no resize-in-place --
    /// call Remove() + Create() instead if the size changed). False + err
    /// on any mismatch, on an `id` this pool did not Create() (including
    /// one Create()'d then already Remove()'d, and one from
    /// RegisterExternalView(), which owns no image to re-upload into), or
    /// on upload failure.
    bool Update(ImTextureID id, const void* rgba, std::string& err);

    /// Retires `id`: no future Create()/Update()/Remove()/
    /// RegisterExternalView() call will find it (an immediate, synchronous
    /// removal from this pool's own bookkeeping), but the underlying
    /// VkImage/VkImageView (if this pool owns them -- not for an id from
    /// RegisterExternalView()) and the imgui descriptor set stay alive
    /// until AdvanceFrame() has walked framesInFlight frames past this
    /// call -- see the class doc comment. No-op if `id` is
    /// ImTextureID_Invalid or not a live id this pool handed out.
    void Remove(ImTextureID id);

    /// Registers imgui's descriptor against an EXTERNALLY-owned view (T10's
    /// Viewport3D: an OffscreenTarget's resolve image, whose VkImage/
    /// VkImageView lifetime Viewport3D itself keeps -- this pool never
    /// creates, uploads into, or destroys it). If `oldId` is not
    /// ImTextureID_Invalid, it is Remove()'d as part of this same call --
    /// the "AddTexture once per resize, not per frame" pattern a
    /// resizable OffscreenTarget needs (call this again only when the
    /// resolve view actually changes, e.g. on a resize that recreated the
    /// target; every other frame just keeps using the ImTextureID this
    /// already returned). Returns ImTextureID_Invalid and fills `err` on
    /// failure; `oldId`, if any, is still Remove()'d either way.
    ImTextureID RegisterExternalView(VkImageView view, VkImageLayout layout, ImTextureID oldId,
                                      std::string& err);

    /// Must be called once per rendered frame -- every call site in this
    /// codebase is a viewer's own Draw(), which already only runs on
    /// visible frames (a document/tab that stops drawing simply defers its
    /// own retirements longer; never unsafely early, since the frame clock
    /// below only ever advances). Uses ImGui::GetFrameCount() as the frame
    /// clock -- every registration call above already runs inside an
    /// active ImGui frame, so this is the same counter Remove() implicitly
    /// stamped its retirement against.
    void AdvanceFrame();

    /// Entries currently awaiting AdvanceFrame() -- for logging/tests.
    size_t PendingDestroyCount() const { return m_queue.PendingCount(); }

private:
    struct Entry {
        Onyx::RenderVk::Image2D image;      // default (img==VK_NULL_HANDLE) for an external-view entry
        VkDescriptorSet         descriptor = VK_NULL_HANDLE;
        uint32_t                width = 0, height = 0;
        bool                    external = false; // true: image/view not owned by this pool
    };

    ImTextureID RegisterView(VkImageView view, VkImageLayout layout, std::string& err);
    void        RetireEntry(Entry entry);

    Onyx::RenderVk::VkContext&           m_ctx;
    uint32_t                             m_framesInFlight;
    std::unordered_map<uint64_t, Entry>  m_live; // key: descriptor set reinterpreted as the ImTextureID handed out
    Onyx::RenderVk::DeferredDestroyQueue m_queue;
};

} // namespace Onyx::App
