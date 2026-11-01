#include "Onyx/Media/MediaView.h"

#include <Onyx/Core/Domain/Texture.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Core/Services/Jobs.h>
#include <Onyx/Ui/FrameClock.h>
#include <Onyx/Ui/SfSymbols.h>
#include <Onyx/Ui/TexturePool.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Viewers/Opening.h>
#include <Onyx/Ui/Widgets.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace Onyx::Media {

namespace {

// The node path as `@i/j/k`: two same-named entries of one document still get distinct view ids.
std::string NodePathText(const Modules::NodePath& path) {
    std::string text = "@";
    for (size_t i = 0; i < path.indices.size(); ++i) {
        if (i) text += '/';
        text += std::to_string(path.indices[i]);
    }
    return text;
}

const char* StateName(PlayState state) {
    switch (state) {
    case PlayState::Stopped: return "Stopped";
    case PlayState::Playing: return "Playing";
    case PlayState::Paused: return "Paused";
    case PlayState::Ended: return "Ended";
    }
    return "?";
}

}

// The open job's result, written on the worker and read by Draw.
struct MediaView::Opening {
    std::mutex mutex;
    std::optional<Result<MediaInfo>> info;
};

MediaView::MediaView(Modules::DocumentId doc, Modules::NodePath node, std::string name,
                     std::shared_ptr<const Vfs::IFile> file, Domain::ByteRange range)
    : m_doc(doc),
      m_node(std::move(node)),
      m_name(std::move(name)),
      m_id("onyx.media." + std::to_string(doc) + "." + NodePathText(m_node) + "." + m_name),
      m_file(std::move(file)),
      m_range(range),
      m_source(std::make_shared<MediaSource>()) {}

MediaView::~MediaView() {
    m_source.reset();
    if (m_pool && m_texture != ImTextureID_Invalid) m_pool->Remove(m_texture);
}

void MediaView::Open(Ui::UiContext& ui) {
    if (m_range.size == 0) {
        m_opened = true;
        m_fault = Error{"media.open", m_name + " has an empty byte range"};
        return;
    }
    m_opening = std::make_shared<Opening>();
    ui.workspace.Jobs().Submit(Ui::kDecodeLane, [source = m_source, opening = m_opening, file = m_file,
                                                 range = m_range](Services::Progress&) {
        auto info = source->Open(file, range.offset, range.size);
        std::lock_guard lock(opening->mutex);
        opening->info = std::move(info);
    });
}

void MediaView::Collect() {
    std::optional<Result<MediaInfo>> info;
    {
        std::lock_guard lock(m_opening->mutex);
        info.swap(m_opening->info);
    }
    if (!info) return;
    m_opened = true;
    m_opening.reset();
    if (!info->ok()) m_fault = info->error();
}

void MediaView::Upload(Ui::TexturePool& pool, VideoFrame frame) {
    Domain::Texture texture;
    texture.name = m_name;
    texture.width = frame.width;
    texture.height = frame.height;
    texture.format = Domain::PixelFormat::Rgba8;
    texture.pixels = std::move(frame.rgba);
    if (m_texture != ImTextureID_Invalid && frame.width == m_width && frame.height == m_height) {
        if (auto streamed = pool.Stream(m_texture, texture); !streamed.ok()) {
            m_fault = streamed.error();
        } else {
            m_texture = streamed.value();
            ++m_uploads;
        }
        return;
    }
    if (m_texture != ImTextureID_Invalid) pool.Remove(m_texture);
    m_texture = ImTextureID_Invalid;
    auto created = pool.Create(texture);
    if (!created.ok()) {
        m_fault = created.error();
        return;
    }
    m_pool = &pool;
    m_texture = created.value();
    m_width = frame.width;
    m_height = frame.height;
    ++m_uploads;
}

void MediaView::DrawTransport() {
    MediaSource& source = *m_source;
    const MediaInfo& info = source.Info();
    const PlayState state = source.State();
    const bool playing = state == PlayState::Playing;
    {
        Ui::Widgets::IconButtonOptions options;
        options.tooltip = playing ? "Pause" : "Play";
        if (Ui::Widgets::IconButton("##play", playing ? ICON_SF_PAUSE : ICON_SF_PLAY, options)) {
            if (playing) {
                source.Pause();
            } else {
                source.Play();
            }
        }
    }
    ImGui::SameLine();
    {
        Ui::Widgets::IconButtonOptions options;
        options.tooltip = "Stop";
        if (Ui::Widgets::IconButton("##stop", ICON_SF_STOP, options)) source.Stop();
    }
    ImGui::SameLine();
    float position = static_cast<float>(source.Position());
    const float duration = static_cast<float>(std::max(info.duration, 0.0));
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 140.0f, 80.0f));
    if (ImGui::SliderFloat("##seek", &position, 0.0f, duration, "%.2f s")) source.Seek(position);
    ImGui::SameLine();
    ImGui::TextUnformatted(ICON_SF_SPEAKER_WAVE_2);
    ImGui::SameLine();
    float volume = source.Volume();
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::SliderFloat("##volume", &volume, 0.0f, 1.0f, "%.2f")) source.SetVolume(volume);

    char line[256];
    std::snprintf(line, sizeof(line), "%s  %ux%u  %.3g fps  %s  %s  %.2f / %.2f s",
                  info.videoCodec.empty() ? "-" : info.videoCodec.c_str(), info.width, info.height, info.fps,
                  info.audioCodec.empty() ? "no audio" : info.audioCodec.c_str(), StateName(state),
                  source.Position(), info.duration);
    ImGui::TextUnformatted(line);
}

void MediaView::DrawPicture() {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1.0f || avail.y < 1.0f) return;
    double aspect = m_source->Info().displayAspect;
    if (aspect <= 0.0) aspect = m_height > 0 ? static_cast<double>(m_width) / m_height : 1.0;
    ImVec2 size(avail.x, static_cast<float>(avail.x / aspect));
    if (size.y > avail.y) size = ImVec2(static_cast<float>(avail.y * aspect), avail.y);
    const ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(cursor.x + (avail.x - size.x) * 0.5f, cursor.y + (avail.y - size.y) * 0.5f));
    ImGui::Image(ImTextureRef(m_texture), size);
}

void MediaView::Draw(Ui::UiContext& ui) {
    if (!m_opened && !m_opening) Open(ui);
    if (m_opening) Collect();
    if (m_fault) {
        Ui::Widgets::StatusText(Services::Severity::Error, (m_fault->code + ": " + m_fault->message).c_str());
        return;
    }
    if (!m_opened) {
        Ui::Widgets::Hint("Opening...");
        ui.clock.RequestRedraw();
        return;
    }
    if (auto frame = m_source->TakeFrame(); frame && ui.textures) Upload(*ui.textures, std::move(*frame));
    if (m_source->State() == PlayState::Playing) ui.clock.RequestRedraw();

    DrawTransport();
    ImGui::Separator();
    if (!ui.textures) {
        Ui::Widgets::Hint("Video frames need a GPU texture pool; only the transport is shown");
        return;
    }
    if (m_texture == ImTextureID_Invalid) {
        Ui::Widgets::Hint("Decoding the first frame");
        return;
    }
    DrawPicture();
}

}
