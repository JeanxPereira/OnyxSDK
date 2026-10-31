#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_map>

#include <glm/vec2.hpp>
#include <imgui.h>

#include "Onyx/Core/Domain/Texture.h"
#include "Onyx/Core/Result.h"
#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/Handles.h"
#include "Onyx/Gpu/Retire.h"
#include "Onyx/Gpu/Upload.h"
#include "Onyx/Ui/TextureBinder.h"

// Error codes (Result<T>::error().code):
//   ui.texture.size    a texture with a zero extent or a pixel vector of the wrong length; Update with a
//                      different extent or format, or with an id the pool does not own
//   ui.texture.format  the device cannot sample the texture's format
// Other failures pass through unchanged: the Gpu layer's codes and the binder's (ui.backend.init from Bind
// or Rebind).
namespace Onyx::Ui {

// Textures a UI shows: uploaded images and external views, each named by an ImTextureID.
//
// Uploads are batched: Create and Update record copies and Flush submits them. A frame that draws a
// new or updated texture must wait on the value Flush returned (Frame::AddWait). Update points the id
// at the new image through ITextureBinder::Rebind after the queue has drained, so the id stays stable;
// it must not be called between recording a command buffer that draws the texture and submitting it.
// It also waits until the graphics queue is idle (a full stall), so call it outside the frame loop; it
// is unsuitable for per-frame updates of hot textures.
//
// Stream is the per-frame path: it never rewrites a descriptor, so a frame still in flight keeps
// sampling the image it was recorded with. Call it at most once per frame for a texture, before the
// frame draws the id it returns; the frame waits on Flush like any other upload. A second Stream in the
// same frame may overwrite an image that frame already draws: the frame then shows the newer texels,
// never a torn copy, because the copy is ordered after earlier work on the queue.
//
// Stream waits on the host in two cases only: all three images of the ring are still in flight (it
// waits for the oldest), or the UploadContext's staging ring needs bytes back (it waits for the oldest
// flushed upload batch, or drains when the uploads recorded since the last Flush fill the staging
// buffer by themselves; see UploadContext).
//
// Must be destroyed before the binder, the UploadContext and the Device. Not thread-safe.
class TexturePool {
public:
    TexturePool(Gpu::Device& device, Gpu::UploadContext& upload, ITextureBinder& binder);
    ~TexturePool();

    TexturePool(const TexturePool&) = delete;
    TexturePool& operator=(const TexturePool&) = delete;

    Result<ImTextureID> Create(const Domain::Texture& texture);
    Result<void> Update(ImTextureID id, const Domain::Texture& texture);
    // Streams a new frame into a texture created by Create, without waiting on the queue: each streamed
    // id owns a ring of three images, each bound once; Stream uploads into the image whose last use the
    // queue has completed (waiting only when all three are in flight) and returns that image's id, which
    // replaces `id` for the next Draw. Remove on any id of the ring removes the whole ring.
    Result<ImTextureID> Stream(ImTextureID id, const Domain::Texture& texture);
    // An external view, for example a render target's resolve image. The pool never owns it.
    Result<ImTextureID> Register(VkImageView view, VkImageLayout layout);
    // Unbinds now; the image is destroyed once the queue completes the submissions that may use it.
    void Remove(ImTextureID id);

    // {0, 0} for an external view or an unknown id.
    glm::uvec2 Size(ImTextureID id) const;
    // Ids the pool holds; a streamed texture counts each id of its ring.
    size_t Count() const { return m_entries.size(); }

    // Submits pending uploads. On failure returns the last flushed value and records Fault().
    Gpu::FenceValue Flush();
    const std::optional<Error>& Fault() const { return m_fault; }

private:
    static constexpr size_t kRingSize = 3;

    struct Slot {
        ImTextureID id = ImTextureID_Invalid;
        Gpu::Image image;
        // The graphics queue's last submitted value when the slot was last drawable.
        Gpu::FenceValue lastUse = 0;
    };
    // The images of a streamed texture. Slot 0 holds the image Create made; the others fill on demand.
    struct Ring {
        std::array<Slot, kRingSize> slots;
        size_t current = 0;
        size_t previous = 0;
    };
    struct Entry {
        // Empty for an id of a ring: the ring holds the image.
        Gpu::Image image;
        glm::uvec2 size{0, 0};
        Domain::PixelFormat format = Domain::PixelFormat::Rgba8;
        bool external = false;
        std::shared_ptr<Ring> ring;
    };

    Result<Gpu::Image> UploadChecked(const Domain::Texture& texture);
    Gpu::Image& ImageOf(ImTextureID id, Entry& entry);

    Gpu::Device& m_device;
    Gpu::UploadContext& m_upload;
    ITextureBinder& m_binder;
    std::unordered_map<ImTextureID, Entry> m_entries;
    bool m_pending = false;
    std::optional<Error> m_fault;
};

}
