#include <doctest/doctest.h>

#include <Onyx/Ui/AudioOutput.h>
#include <Onyx/Ui/Viewers/AudioView.h>

#include "Support/ObxViewerFixture.h"

using namespace Onyx;
using namespace Onyx::Ui;

namespace {

Domain::Audio Ramp(uint32_t frames, uint32_t channels) {
    Domain::Audio audio;
    audio.sampleRate = 100;
    audio.channels = channels;
    for (uint32_t f = 0; f < frames; ++f) {
        for (uint32_t c = 0; c < channels; ++c) {
            audio.samples.push_back(static_cast<float>(f) / static_cast<float>(frames));
        }
    }
    return audio;
}

struct FakeOutput : IAudioOutput {
    Result<void> Play(const Domain::Audio&) override {
        playing = true;
        ++plays;
        return Result<void>::Ok();
    }
    void Stop() override { playing = false; }
    bool Playing() const override { return playing; }
    double Position() const override { return 0.5; }

    bool playing = false;
    int plays = 0;
};

}

TEST_CASE("AudioView reports the duration and one peak pair per column") {
    AudioView view(1, Modules::NodePath{{0}}, "a", Ramp(200, 2));
    CHECK(view.Duration() == doctest::Approx(2.0));

    const auto& peaks = view.Peaks(4);
    REQUIRE(peaks.size() == 4);
    CHECK(peaks[0].min == doctest::Approx(0.0f));
    CHECK(peaks[0].max < peaks[3].max);
    CHECK(peaks[3].max == doctest::Approx(199.0f / 200.0f));
    for (const WavePeak& p : peaks) CHECK(p.min <= p.max);

    CHECK(view.Peaks(8).size() == 8);
    CHECK(view.Peaks(0).empty());
}

TEST_CASE("AudioView of an empty clip has no duration and flat peaks") {
    AudioView view(1, Modules::NodePath{{0}}, "a", Domain::Audio{});
    CHECK(view.Duration() == 0.0);
    for (const WavePeak& p : view.Peaks(16)) {
        CHECK(p.min == 0.0f);
        CHECK(p.max == 0.0f);
    }
}

TEST_CASE("AudioView draws without an output and with one") {
    HeadlessImGui imgui;
    TestSupport::ObxViewerFixture fx;
    UiContext ui = fx.Make();
    AudioView view(1, Modules::NodePath{{0}}, "a", Ramp(400, 1));
    TestSupport::DrawFrames(imgui, view, ui);

    FakeOutput output;
    ui.audio = &output;
    TestSupport::DrawFrames(imgui, view, ui);
    CHECK(output.plays == 0);
}
