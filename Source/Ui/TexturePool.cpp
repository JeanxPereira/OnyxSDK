#include "Onyx/Ui/TexturePool.h"

#include <cstdint>
#include <string>
#include <utility>

namespace Onyx::Ui {

namespace {

Error SizeError(std::string message) {
    return Error{"ui.texture.size", std::move(message), 0};
}

Error Translate(const Error& error) {
    if (error.code == "gpu.upload.size") {
        return Error{"ui.texture.size", error.message, error.native};
    }
    if (error.code == "gpu.upload.format") {
        return Error{"ui.texture.format", error.message, error.native};
    }
    return error;
}

}

TexturePool::TexturePool(Gpu::Device& device, Gpu::UploadContext& upload, ITextureBinder& binder)
    : m_device(device), m_upload(upload), m_binder(binder) {}

TexturePool::~TexturePool() {
    Flush();
    for (auto& [id, entry] : m_entries) {
        m_binder.Unbind(id);
    }
    m_entries.clear();
}

Result<Gpu::Image> TexturePool::UploadChecked(const Domain::Texture& texture) {
    if (texture.width == 0 || texture.height == 0) {
        return Result<Gpu::Image>::Fail(SizeError("texture '" + texture.name + "' has a zero extent"));
    }
    if (texture.pixels.size() != Domain::ExpectedByteSize(texture.format, texture.width, texture.height)) {
        return Result<Gpu::Image>::Fail(SizeError("texture '" + texture.name + "' has the wrong pixel byte count"));
    }
    auto image = m_upload.Upload(texture);
    if (!image.ok()) {
        return Result<Gpu::Image>::Fail(Translate(image.error()));
    }
    m_pending = true;
    return image;
}

Result<ImTextureID> TexturePool::Create(const Domain::Texture& texture) {
    auto image = UploadChecked(texture);
    if (!image.ok()) {
        return Result<ImTextureID>::Fail(image.error());
    }
    auto id = m_binder.Bind(image.value().view.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_NULL_HANDLE);
    if (!id.ok()) {
        Flush();
        return id;
    }
    Entry entry;
    entry.image = std::move(image.value());
    entry.size = {texture.width, texture.height};
    entry.format = texture.format;
    m_entries.emplace(id.value(), std::move(entry));
    return id;
}

Result<void> TexturePool::Update(ImTextureID id, const Domain::Texture& texture) {
    auto it = m_entries.find(id);
    if (it == m_entries.end() || it->second.external) {
        return Result<void>::Fail(SizeError("texture id is not owned by the pool"));
    }
    Entry& entry = it->second;
    if (entry.size != glm::uvec2(texture.width, texture.height) || entry.format != texture.format) {
        return Result<void>::Fail(SizeError("texture '" + texture.name + "' changes the extent or format"));
    }

    // The old image must have its copies submitted before it retires, and no submission may still
    // sample the descriptor that is about to be rewritten.
    Flush();
    Gpu::Queue& queue = m_device.GraphicsQueue();
    auto waited = queue.Wait(queue.LastSubmitted());
    if (!waited.ok()) {
        return waited;
    }
    auto image = UploadChecked(texture);
    if (!image.ok()) {
        return Result<void>::Fail(image.error());
    }

    VkDescriptorImageInfo info{};
    info.imageView = image.value().view.Get();
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = reinterpret_cast<VkDescriptorSet>(static_cast<uintptr_t>(id));
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &info;
    m_device.Fn().vkUpdateDescriptorSets(m_device.Handle(), 1, &write, 0, nullptr);

    entry.image = std::move(image.value());
    return Result<void>::Ok();
}

Result<ImTextureID> TexturePool::Register(VkImageView view, VkImageLayout layout) {
    auto id = m_binder.Bind(view, layout, VK_NULL_HANDLE);
    if (!id.ok()) {
        return id;
    }
    Entry entry;
    entry.external = true;
    m_entries.emplace(id.value(), std::move(entry));
    return id;
}

void TexturePool::Remove(ImTextureID id) {
    auto it = m_entries.find(id);
    if (it == m_entries.end()) {
        return;
    }
    if (!it->second.external) {
        Flush();
    }
    m_binder.Unbind(id);
    m_entries.erase(it);
}

glm::uvec2 TexturePool::Size(ImTextureID id) const {
    auto it = m_entries.find(id);
    return it == m_entries.end() ? glm::uvec2(0, 0) : it->second.size;
}

Gpu::FenceValue TexturePool::Flush() {
    if (!m_pending) {
        return m_upload.LastFlushed();
    }
    m_pending = false;
    auto flushed = m_upload.Flush();
    if (!flushed.ok()) {
        m_fault = flushed.error();
        return m_upload.LastFlushed();
    }
    return flushed.value();
}

}
