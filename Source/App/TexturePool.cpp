// Compiled twice (Onyx_Shell AND Onyx_Media -- see CMakeLists.txt's
// ONYX_MEDIA_SOURCES comment for why) -- keep this file self-contained and
// free of any state that would need to be process-wide; see
// Include/Onyx/App/TexturePool.h's class doc comment for the ownership
// model that makes that safe.
#include <Onyx/App/TexturePool.h>

#include <Onyx/RenderVk/VkContext.h>

#include "imgui_impl_vulkan.h"

namespace Onyx::App {

using Onyx::RenderVk::Image2D;
using Onyx::RenderVk::Resources;

namespace {
inline ImTextureID ToTexId(VkDescriptorSet set) {
    return static_cast<ImTextureID>(reinterpret_cast<uint64_t>(set));
}
} // namespace

TexturePool::TexturePool(Onyx::RenderVk::VkContext& ctx, uint32_t framesInFlight)
    : m_ctx(ctx), m_framesInFlight(framesInFlight) {}

TexturePool::~TexturePool() {
    // Shutdown-order guard (T10 disclosed gap -- see task-10-report.md's
    // Concerns): Window::exitVulkan() clears the process-wide accessor
    // (Onyx::RenderVk::SetGlobalContext(nullptr)) as the FIRST thing it
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
    if (Onyx::RenderVk::GetGlobalContext() == nullptr) {
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
    // srcLayout=SHADER_READ_ONLY_OPTIMAL: this image already went through
    // Create()'s UploadImage() (or a previous Update()) and was left there
    // -- see Resources::UploadImage's own doc comment for why the default
    // UNDEFINED would be wrong (and would raise a validation error) here.
    return Resources::UploadImage(m_ctx, it->second.image, rgba, err,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
    if (oldId != ImTextureID_Invalid)
        Remove(oldId);
    return newId;
}

void TexturePool::RetireEntry(Entry entry) {
    const uint64_t retiredFrame = static_cast<uint64_t>(ImGui::GetFrameCount());
    Onyx::RenderVk::VkContext* ctx = &m_ctx;
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
