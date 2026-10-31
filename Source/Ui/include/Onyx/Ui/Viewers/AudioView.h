#pragma once

#include <Onyx/Core/Domain/Audio.h>
#include <Onyx/Ui/Panel.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Onyx::Ui {

struct WavePeak {
    float min = 0.0f;
    float max = 0.0f;
};

// A waveform and the format of a decoded clip. Play and Stop drive the context's audio output when
// it has one.
class AudioView : public IDocumentView {
public:
    AudioView(Modules::DocumentId doc, Modules::NodePath node, std::string name, Domain::Audio audio);

    std::string_view Id() const override { return m_id; }
    std::string_view Title() const override { return m_name; }
    void Draw(UiContext& ui) override;
    Modules::DocumentId Document() const override { return m_doc; }
    Modules::NodePath Node() const override { return m_node; }

    // Seconds; zero for a clip without a sample rate or channels.
    double Duration() const;
    // One min/max pair per pixel column over every channel, cached for the last width asked.
    const std::vector<WavePeak>& Peaks(size_t width);

private:
    Modules::DocumentId m_doc;
    Modules::NodePath m_node;
    std::string m_name;
    std::string m_id;
    Domain::Audio m_audio;
    std::vector<WavePeak> m_peaks;
    size_t m_peaksWidth = 0;
    std::optional<std::string> m_playError;
};

}
