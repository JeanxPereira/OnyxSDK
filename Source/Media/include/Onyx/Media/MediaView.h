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

// A video clip with a transport bar: play/pause, stop, a seek slider, volume, and the codecs and frame
// rate. The source opens on the first draw. Each new frame is uploaded through the context's texture
// pool (created once, updated afterwards); without a pool the view draws its status text alone. The
// destructor stops the source, joining its decode thread, then removes the texture, so the pool must
// outlive the view.
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

    MediaSource& Source() { return *m_source; }
    // The open or upload failure, if any.
    const std::optional<Error>& Fault() const { return m_fault; }
    ImTextureID Texture() const { return m_texture; }

private:
    void Open();
    void Upload(Ui::TexturePool& pool, VideoFrame frame);
    void DrawTransport();
    void DrawPicture();

    Modules::DocumentId m_doc;
    Modules::NodePath m_node;
    std::string m_name;
    std::string m_id;
    std::shared_ptr<const Vfs::IFile> m_file;
    Domain::ByteRange m_range;
    std::unique_ptr<MediaSource> m_source;
    bool m_opened = false;
    std::optional<Error> m_fault;
    Ui::TexturePool* m_pool = nullptr;
    ImTextureID m_texture = ImTextureID_Invalid;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

}
