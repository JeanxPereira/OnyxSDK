#include "Onyx/Ui/TexturePool.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

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

Gpu::Image& TexturePool::ImageOf(ImTextureID id, Entry& entry) {
    if (entry.ring) {
        for (Slot& slot : entry.ring->slots) {
            if (slot.id == id) return slot.image;
        }
    }
    return entry.image;
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

    auto rebound = m_binder.Rebind(id, image.value().view.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!rebound.ok()) {
        return rebound;
    }

    ImageOf(id, entry) = std::move(image.value());
    return Result<void>::Ok();
}

Result<ImTextureID> TexturePool::Stream(ImTextureID id, const Domain::Texture& texture) {
    using Out = Result<ImTextureID>;
    auto it = m_entries.find(id);
    if (it == m_entries.end() || it->second.external) {
        return Out::Fail(SizeError("texture id is not owned by the pool"));
    }
    if (it->second.size != glm::uvec2(texture.width, texture.height) || it->second.format != texture.format) {
        return Out::Fail(SizeError("texture '" + texture.name + "' changes the extent or format"));
    }
    if (texture.pixels.size() != Domain::ExpectedByteSize(texture.format, texture.width, texture.height)) {
        return Out::Fail(SizeError("texture '" + texture.name + "' has the wrong pixel byte count"));
    }
    if (!it->second.ring) {
        auto ring = std::make_shared<Ring>();
        ring->slots[0].id = id;
        ring->slots[0].image = std::move(it->second.image);
        it->second.ring = std::move(ring);
    }
    // Held by value: emplacing a new id below may rehash m_entries.
    const std::shared_ptr<Ring> ring = it->second.ring;

    // Every frame that drew the current or the previous image was recorded before this call, and was
    // submitted by now unless it is the frame being built, which the next Stream covers.
    Gpu::Queue& queue = m_device.GraphicsQueue();
    const Gpu::FenceValue submitted = queue.LastSubmitted();
    ring->slots[ring->current].lastUse = std::max(ring->slots[ring->current].lastUse, submitted);
    ring->slots[ring->previous].lastUse = std::max(ring->slots[ring->previous].lastUse, submitted);

    const size_t next = (ring->current + 1) % kRingSize;
    Slot& slot = ring->slots[next];
    if (!slot.image.handle) {
        auto image = UploadChecked(texture);
        if (!image.ok()) {
            return Out::Fail(image.error());
        }
        auto bound = m_binder.Bind(image.value().view.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_NULL_HANDLE);
        if (!bound.ok()) {
            Flush();
            return bound;
        }
        slot.id = bound.value();
        slot.image = std::move(image.value());
        Entry entry;
        entry.size = {texture.width, texture.height};
        entry.format = texture.format;
        entry.ring = ring;
        m_entries.emplace(slot.id, std::move(entry));
    } else {
        // The next slot is the oldest: when its last frame is still in flight, so are the other two.
        if (slot.lastUse > queue.Completed()) {
            auto waited = queue.Wait(slot.lastUse);
            if (!waited.ok()) {
                return Out::Fail(waited.error());
            }
        }
        auto copied = m_upload.UploadInto(texture, slot.image);
        if (!copied.ok()) {
            return Out::Fail(Translate(copied.error()));
        }
        m_pending = true;
    }
    ring->previous = ring->current;
    ring->current = next;
    return Out::Ok(slot.id);
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
    std::vector<ImTextureID> ids{id};
    if (const std::shared_ptr<Ring> ring = it->second.ring) {
        ids.clear();
        for (const Slot& slot : ring->slots) {
            if (slot.id != ImTextureID_Invalid) ids.push_back(slot.id);
        }
    }
    for (ImTextureID each : ids) {
        m_binder.Unbind(each);
        m_entries.erase(each);
    }
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
