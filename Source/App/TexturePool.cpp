// Compiled twice (Onyx_Shell AND Onyx_Media -- see CMakeLists.txt's
// ONYX_MEDIA_SOURCES comment for why) -- keep this file self-contained and
// free of any state that would need to be process-wide; see
// Include/Onyx/App/TexturePool.h's class doc comment for the ownership
// model that makes that safe.
#include <Onyx/App/TexturePool.h>

#include <Onyx/RenderVk/VkContext.h>

#include "imgui_impl_vulkan.h"

#include <cstring>

namespace Onyx::App {

using Onyx::Rendering::Buffer;
using Onyx::Rendering::Image2D;
using Onyx::Rendering::Resources;

namespace {
inline ImTextureID ToTexId(VkDescriptorSet set) {
    return static_cast<ImTextureID>(reinterpret_cast<uint64_t>(set));
}

// TexturePool::Update()'s own staged re-upload -- deliberately NOT
// Resources::UploadImage (T10 fix-round-1, reviewer-traced HIGH write-
// after-read hazard). UploadImage's pre-copy barrier always uses
// srcStage=TOP_OF_PIPE/srcAccess=NONE, correct for a FRESH image (every
// other caller) but not for one this same frame's swapchain submission
// may still be sampling: `dst` is always SHADER_READ_ONLY_OPTIMAL when
// Update() calls this (Create()'s own UploadImage() and every prior
// Update() both always leave it there), and some OTHER queue submission
// -- the swapchain frame's ImGui draw, with no semaphore tying it to this
// one -- may still be reading it in its fragment shader. This function's
// pre-copy barrier is a queue-scoped acquire against exactly that read
// (srcStage=FRAGMENT_SHADER, srcAccess=SHADER_SAMPLED_READ) instead of
// UploadImage's generic UNDEFINED-path masks.
bool ReuploadFromShaderRead(Onyx::Rendering::VkContext& ctx, Image2D& dst, const void* rgba,
                             std::string& err) {
    const VkDeviceSize size = static_cast<VkDeviceSize>(dst.width) * dst.height * 4;

    Buffer staging = Resources::CreateBuffer(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                              VMA_MEMORY_USAGE_CPU_ONLY, err);
    if (staging.buf == VK_NULL_HANDLE) {
        err = "TexturePool::Update: staging buffer: " + err;
        return false;
    }

    VmaAllocationInfo stagingInfo{};
    vmaGetAllocationInfo(ctx.Allocator(), staging.alloc, &stagingInfo);
    if (!stagingInfo.pMappedData) {
        err = "TexturePool::Update: staging buffer is not host-mapped";
        Resources::Destroy(ctx, staging);
        return false;
    }
    std::memcpy(stagingInfo.pMappedData, rgba, static_cast<size_t>(size));

    bool ok = Resources::OneShot(ctx, [&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier2 toDst{};
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        toDst.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        toDst.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        toDst.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toDst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toDst.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = dst.img;
        toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo dep1{};
        dep1.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep1.imageMemoryBarrierCount = 1;
        dep1.pImageMemoryBarriers = &toDst;
        vkCmdPipelineBarrier2(cmd, &dep1);

        VkBufferImageCopy copy{};
        copy.bufferOffset = 0;
        copy.bufferRowLength = 0;
        copy.bufferImageHeight = 0;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageOffset = {0, 0, 0};
        copy.imageExtent = {dst.width, dst.height, 1};
        vkCmdCopyBufferToImage(cmd, staging.buf, dst.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        VkImageMemoryBarrier2 toRead{};
        toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        toRead.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toRead.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        toRead.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.image = dst.img;
        toRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo dep2{};
        dep2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep2.imageMemoryBarrierCount = 1;
        dep2.pImageMemoryBarriers = &toRead;
        vkCmdPipelineBarrier2(cmd, &dep2);
    }, err);

    Resources::Destroy(ctx, staging);
    return ok;
}
} // namespace

TexturePool::TexturePool(Onyx::Rendering::VkContext& ctx, uint32_t framesInFlight)
    : m_ctx(ctx), m_framesInFlight(framesInFlight) {}

TexturePool::~TexturePool() {
    // Shutdown-order guard (T10 disclosed gap -- see task-10-report.md's
    // Concerns): Window::exitVulkan() clears the process-wide accessor
    // (Onyx::Rendering::SetGlobalContext(nullptr)) as the FIRST thing it
    // does, before destroying a single Vulkan handle -- but Window's own
    // VkContext member is destroyed only once ~Window()'s later, implicit
    // member destructors run, which is AFTER m_app (and therefore every
    // open document/viewer, including whatever owns this pool) has already
    // been destroyed... EXCEPT this pool's own m_ctx is a bound reference
    // to that same object, so if the app is instead closed with a
    // Vulkan-texture-owning document still open, this destructor runs
    // with m_ctx possibly already dangling. Checking the global accessor
    // (rather than trusting m_ctx directly) tells us which case this is:
    // non-null means the same live VkContext m_ctx is still bound to;
    // null means it is not safe to touch m_ctx at all, so every entry
    // below is deliberately leaked instead of risking UB -- the process is
    // exiting either way.
    if (Onyx::Rendering::GetGlobalContext() == nullptr) {
        m_live.clear();
        return;
    }

    // Every entry still live (never Remove()'d by its owner) and every
    // already-Remove()'d-but-not-yet-collected entry must be torn down
    // before this object goes away -- there is no next AdvanceFrame() call
    // coming. vkDeviceWaitIdle first (matches Window::~Window()'s own
    // shutdown-order rule for exactly the same reason: ImGui's Vulkan
    // backend, and this image, may still be referenced by a command buffer
    // the GPU has not finished executing) makes CollectAll() -- and
    // destroying every still-live entry below -- unconditionally safe.
    if (m_ctx.Device() != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_ctx.Device());

    m_queue.CollectAll();

    for (auto& [key, entry] : m_live) {
        if (entry.descriptor != VK_NULL_HANDLE)
            ImGui_ImplVulkan_RemoveTexture(entry.descriptor);
        if (!entry.external)
            Resources::Destroy(m_ctx, entry.image);
    }
    m_live.clear();
}

ImTextureID TexturePool::Create(uint32_t width, uint32_t height, const void* rgba, std::string& err) {
    Image2D img = Resources::CreateImage2D(m_ctx, width, height, VK_FORMAT_R8G8B8A8_UNORM,
                                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                            VK_SAMPLE_COUNT_1_BIT, err);
    if (img.img == VK_NULL_HANDLE) {
        err = "TexturePool::Create: " + err;
        return ImTextureID_Invalid;
    }

    if (!Resources::UploadImage(m_ctx, img, rgba, err)) {
        err = "TexturePool::Create: " + err;
        Resources::Destroy(m_ctx, img);
        return ImTextureID_Invalid;
    }

    // UploadImage() leaves `img` in SHADER_READ_ONLY_OPTIMAL on success --
    // exactly the layout AddTexture wants.
    VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (set == VK_NULL_HANDLE) {
        err = "TexturePool::Create: ImGui_ImplVulkan_AddTexture returned VK_NULL_HANDLE";
        Resources::Destroy(m_ctx, img);
        return ImTextureID_Invalid;
    }

    Entry entry;
    entry.image = img;
    entry.descriptor = set;
    entry.width = width;
    entry.height = height;
    entry.external = false;

    const uint64_t key = reinterpret_cast<uint64_t>(set);
    m_live.emplace(key, entry);
    return ToTexId(set);
}

bool TexturePool::Update(ImTextureID id, const void* rgba, std::string& err) {
    auto it = m_live.find(static_cast<uint64_t>(id));
    if (it == m_live.end() || it->second.external) {
        err = "TexturePool::Update: id is not a live, pool-owned texture";
        return false;
    }
    // ReuploadFromShaderRead, not Resources::UploadImage -- see that
    // function's own doc comment (top of this file) for why: this image
    // is always SHADER_READ_ONLY_OPTIMAL here (Create()'s UploadImage()
    // and every prior Update() both leave it there), and a same-queue
    // reader (the swapchain frame's ImGui draw) may still be sampling it
    // with no semaphore tying that submission to this one.
    return ReuploadFromShaderRead(m_ctx, it->second.image, rgba, err);
}

void TexturePool::Remove(ImTextureID id) {
    if (id == ImTextureID_Invalid) return;
    auto it = m_live.find(static_cast<uint64_t>(id));
    if (it == m_live.end()) return;

    Entry entry = it->second;
    m_live.erase(it);
    RetireEntry(std::move(entry));
}

ImTextureID TexturePool::RegisterView(VkImageView view, VkImageLayout layout, std::string& err) {
    VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(view, layout);
    if (set == VK_NULL_HANDLE) {
        err = "TexturePool::RegisterView: ImGui_ImplVulkan_AddTexture returned VK_NULL_HANDLE";
        return ImTextureID_Invalid;
    }

    Entry entry;
    entry.descriptor = set;
    entry.external = true;

    const uint64_t key = reinterpret_cast<uint64_t>(set);
    m_live.emplace(key, entry);
    return ToTexId(set);
}

ImTextureID TexturePool::RegisterExternalView(VkImageView view, VkImageLayout layout, ImTextureID oldId,
                                               std::string& err) {
    ImTextureID newId = RegisterView(view, layout, err);
    if (newId == ImTextureID_Invalid) {
        // T10 fix-round-1 (LOW): keep the old (still valid, still working)
        // descriptor alive on failure instead of retiring it unconditionally
        // -- Viewport3D (this pool's only caller) keeps displaying the
        // previous frame's resolve view rather than losing its texture
        // entirely over one failed resize.
        return ImTextureID_Invalid;
    }
    if (oldId != ImTextureID_Invalid)
        Remove(oldId);
    return newId;
}

void TexturePool::RetireEntry(Entry entry) {
    const uint64_t retiredFrame = static_cast<uint64_t>(ImGui::GetFrameCount());
    Onyx::Rendering::VkContext* ctx = &m_ctx;
    m_queue.Retire(retiredFrame, [ctx, entry]() mutable {
        if (entry.descriptor != VK_NULL_HANDLE)
            ImGui_ImplVulkan_RemoveTexture(entry.descriptor);
        if (!entry.external)
            Resources::Destroy(*ctx, entry.image);
    });
}

void TexturePool::AdvanceFrame() {
    m_queue.Collect(static_cast<uint64_t>(ImGui::GetFrameCount()), m_framesInFlight);
}

} // namespace Onyx::App
