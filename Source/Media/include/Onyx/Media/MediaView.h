#pragma once

#include <Onyx/Core/Domain/ByteRange.h>
#include <Onyx/Core/Result.h>
#include <Onyx/Core/Vfs/IFile.h>
#include <Onyx/Media/MediaSource.h>
#include <Onyx/Ui/Panel.h>

#include <imgui.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Onyx::Ui {
class TexturePool;
}

namespace Onyx::Media {

// A video clip with a transport bar: play/pause, stop, a seek slider, volume, and the codecs and
// frame rate. The first draw submits the source's Open to the workspace's decode lane and returns;
// until the result is in, Draw shows "Opening..." and asks the frame clock for another frame. A
// range of size zero fails with media.open and opens nothing. Each new frame is uploaded through
// the context's texture pool (created once, updated afterwards); without a pool the view draws its
// status text alone. The destructor releases the source, which stops and joins its decode thread
// unless the open job still holds it, then removes the texture, so the pool must outlive the view.
class MediaView final : public Ui::IDocumentView {
public:
    MediaView(Modules::DocumentId doc, Modules::NodePath node, std::string name,
              std::shared_ptr<const Vfs::IFile> file, Domain::ByteRange range);
    ~MediaView() override;

    MediaView(const MediaView&) = delete;
    MediaView& operator=(const MediaView&) = delete;

    std::string_view Id() const override { return m_id; }
    std::string_view Title() const override { return m_name; }
    void Draw(Ui::UiContext& ui) override;
    Modules::DocumentId Document() const override { return m_doc; }
    Modules::NodePath Node() const override { return m_node; }

    // Only once Opened() is true may the source be used: Open runs on a worker until then.
    MediaSource& Source() { return *m_source; }
    // True once Draw picked up the open result, or failed before opening.
    bool Opened() const { return m_opened; }
    // The open or upload failure, if any.
    const std::optional<Error>& Fault() const { return m_fault; }
    ImTextureID Texture() const { return m_texture; }
    // How many decoded frames have reached the pool, the first through Create and the rest through Update.
    uint64_t Uploads() const { return m_uploads; }

private:
    struct Opening;

    void Open(Ui::UiContext& ui);
    void Collect();
    void Upload(Ui::TexturePool& pool, VideoFrame frame);
    void DrawTransport();
    void DrawPicture();

    Modules::DocumentId m_doc;
    Modules::NodePath m_node;
    std::string m_name;
    std::string m_id;
    std::shared_ptr<const Vfs::IFile> m_file;
    Domain::ByteRange m_range;
    // Shared with the open job, which may outlive the view.
    std::shared_ptr<MediaSource> m_source;
    std::shared_ptr<Opening> m_opening;
    bool m_opened = false;
    std::optional<Error> m_fault;
    Ui::TexturePool* m_pool = nullptr;
    ImTextureID m_texture = ImTextureID_Invalid;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint64_t m_uploads = 0;
};

}
