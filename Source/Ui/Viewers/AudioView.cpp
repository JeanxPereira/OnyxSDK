#include "Onyx/Ui/Viewers/AudioView.h"

#include <Onyx/Ui/AudioOutput.h>
#include <Onyx/Ui/FrameClock.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Widgets.h>

#include <imgui.h>

#include <algorithm>
#include <utility>

namespace Onyx::Ui {

AudioView::AudioView(Modules::DocumentId doc, Modules::NodePath node, std::string name, Domain::Audio audio)
    : m_doc(doc),
      m_node(std::move(node)),
      m_name(std::move(name)),
      m_id("onyx.audio." + std::to_string(doc) + "." + m_name),
      m_audio(std::move(audio)) {}

double AudioView::Duration() const {
    if (m_audio.sampleRate == 0 || m_audio.channels == 0) return 0.0;
    const double frames = static_cast<double>(m_audio.samples.size()) / static_cast<double>(m_audio.channels);
    return frames / static_cast<double>(m_audio.sampleRate);
}

const std::vector<WavePeak>& AudioView::Peaks(size_t width) {
    if (width == m_peaksWidth && m_peaks.size() == width) return m_peaks;
    m_peaksWidth = width;
    m_peaks.assign(width, WavePeak{});
    const size_t total = m_audio.samples.size();
    if (total == 0) return m_peaks;
    for (size_t x = 0; x < width; ++x) {
        const size_t begin = std::min(x * total / width, total - 1);
        const size_t end = std::clamp((x + 1) * total / width, begin + 1, total);
        const auto range = std::minmax_element(m_audio.samples.begin() + static_cast<std::ptrdiff_t>(begin),
                                               m_audio.samples.begin() + static_cast<std::ptrdiff_t>(end));
        m_peaks[x] = WavePeak{*range.first, *range.second};
    }
    return m_peaks;
}

void AudioView::Draw(UiContext& ui) {
    const double duration = Duration();
    ImGui::Text("%u Hz  %u ch  %.2f s  %zu samples", m_audio.sampleRate, m_audio.channels, duration, m_audio.samples.size());

    if (ui.audio) {
        const bool playing = ui.audio->Playing();
        if (Widgets::ToolbarButton(playing ? "Stop" : "Play")) {
            if (playing) {
                ui.audio->Stop();
            } else if (auto started = ui.audio->Play(m_audio); !started.ok()) {
                m_playError = started.error().code + ": " + started.error().message;
            } else {
                m_playError.reset();
                m_position = 0.0;
            }
        }
        if (ui.audio->Playing()) {
            ImGui::SameLine();
            if (Widgets::ToolbarButton(ui.audio->Paused() ? "Resume" : "Pause")) {
                if (ui.audio->Paused()) {
                    ui.audio->Resume();
                } else {
                    ui.audio->Pause();
                }
            }
            m_position = ui.audio->Position();
            if (!ui.audio->Paused()) ui.clock.RequestRedraw();
        }
    } else {
        Widgets::Hint("No audio output is available");
    }
    if (m_playError) Widgets::StatusText(Services::Severity::Error, m_playError->c_str());
    ImGui::Separator();

    ImVec2 size = ImGui::GetContentRegionAvail();
    size.y = std::max(size.y, 60.0f);
    if (size.x < 2.0f) return;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##wave", size);
    if (ui.audio && ui.audio->Playing() && duration > 0.0 && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        const float fraction = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / size.x, 0.0f, 1.0f);
        m_position = static_cast<double>(fraction) * duration;
        ui.audio->Seek(m_position);
    }

    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    const float mid = origin.y + size.y * 0.5f;
    const float half = size.y * 0.5f - 2.0f;
    list->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg));
    list->AddLine(ImVec2(origin.x, mid), ImVec2(end.x, mid), ImGui::GetColorU32(ImGuiCol_Border));

    const std::vector<WavePeak>& peaks = Peaks(static_cast<size_t>(size.x));
    const ImU32 wave = ImGui::GetColorU32(ImGuiCol_PlotLines);
    for (size_t x = 0; x < peaks.size(); ++x) {
        const float px = origin.x + static_cast<float>(x) + 0.5f;
        const float top = mid - std::clamp(peaks[x].max, -1.0f, 1.0f) * half;
        const float bottom = mid - std::clamp(peaks[x].min, -1.0f, 1.0f) * half;
        list->AddLine(ImVec2(px, top), ImVec2(px, std::max(bottom, top + 1.0f)), wave);
    }

    if (ui.audio && ui.audio->Playing() && duration > 0.0) {
        const float fraction = static_cast<float>(std::clamp(ui.audio->Position() / duration, 0.0, 1.0));
        const float px = origin.x + fraction * size.x;
        list->AddLine(ImVec2(px, origin.y), ImVec2(px, end.y), ImGui::GetColorU32(ImGuiCol_PlotHistogram), 2.0f);
    }
}

}
