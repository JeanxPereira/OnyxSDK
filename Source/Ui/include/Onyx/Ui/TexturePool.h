#pragma once

#include <cstddef>
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
// Must be destroyed before the binder, the UploadContext and the Device. Not thread-safe.
class TexturePool {
public:
    TexturePool(Gpu::Device& device, Gpu::UploadContext& upload, ITextureBinder& binder);
    ~TexturePool();

    TexturePool(const TexturePool&) = delete;
    TexturePool& operator=(const TexturePool&) = delete;

    Result<ImTextureID> Create(const Domain::Texture& texture);
    Result<void> Update(ImTextureID id, const Domain::Texture& texture);
    // An external view, for example a render target's resolve image. The pool never owns it.
    Result<ImTextureID> Register(VkImageView view, VkImageLayout layout);
    // Unbinds now; the image is destroyed once the queue completes the submissions that may use it.
    void Remove(ImTextureID id);

    // {0, 0} for an external view or an unknown id.
    glm::uvec2 Size(ImTextureID id) const;
    size_t Count() const { return m_entries.size(); }

    // Submits pending uploads. On failure returns the last flushed value and records Fault().
    Gpu::FenceValue Flush();
    const std::optional<Error>& Fault() const { return m_fault; }

private:
    struct Entry {
        Gpu::Image image;
        glm::uvec2 size{0, 0};
        Domain::PixelFormat format = Domain::PixelFormat::Rgba8;
        bool external = false;
    };

    Result<Gpu::Image> UploadChecked(const Domain::Texture& texture);

    Gpu::Device& m_device;
    Gpu::UploadContext& m_upload;
    ITextureBinder& m_binder;
    std::unordered_map<ImTextureID, Entry> m_entries;
    bool m_pending = false;
    std::optional<Error> m_fault;
};

}
