#include <doctest/doctest.h>

#include <Onyx/Ui/AudioOutput.h>
#include <Onyx/Ui/Viewers/AudioView.h>

#include "Support/ObxWorkspace.h"

#include <vector>

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
        paused = false;
        position = 0.0;
        ++plays;
        return Result<void>::Ok();
    }
    void Stop() override {
        playing = false;
        paused = false;
    }
    void Pause() override {
        paused = true;
        ++pauses;
    }
    void Resume() override {
        paused = false;
        ++resumes;
    }
    void Seek(double seconds) override {
        position = seconds;
        seeks.push_back(seconds);
    }
    bool Paused() const override { return paused; }
    bool Playing() const override { return playing; }
    double Position() const override { return position; }

    bool playing = false;
    bool paused = false;
    double position = 0.5;
    int plays = 0;
    int pauses = 0;
    int resumes = 0;
    std::vector<double> seeks;
};

// One frame of a 480x320 window around the view, with the mouse at (x, y).
void Frame(HeadlessImGui& imgui, AudioView& view, UiContext& ui, float x, float y, bool down) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
    imgui.NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(480, 320));
    ImGui::Begin("viewer", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    view.Draw(ui);
    ImGui::End();
    imgui.EndFrame();
}

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
    ObxUi fx;
    UiContext ui = fx.Make();
    AudioView view(1, Modules::NodePath{{0}}, "a", Ramp(400, 1));
    DrawFrames(imgui, view, ui);

    FakeOutput output;
    ui.audio = &output;
    DrawFrames(imgui, view, ui);
    CHECK(output.plays == 0);
}

TEST_CASE("Pause keeps the position and Resume continues") {
    HeadlessImGui imgui;
    ObxUi fx;
    UiContext ui = fx.Make();
    FakeOutput output;
    ui.audio = &output;
    AudioView view(1, Modules::NodePath{{0}}, "a", Ramp(400, 1));

    REQUIRE(output.Play(Domain::Audio{}).ok());
    output.position = 1.25;
    DrawFrames(imgui, view, ui);
    CHECK(view.Position() == doctest::Approx(1.25));

    output.Pause();
    output.pauses = 0;
    DrawFrames(imgui, view, ui);
    CHECK(output.Playing());
    CHECK(output.Paused());
    CHECK(view.Position() == doctest::Approx(1.25));
    CHECK(output.pauses == 0);

    output.Resume();
    output.position = 2.0;
    DrawFrames(imgui, view, ui);
    CHECK_FALSE(output.Paused());
    CHECK(view.Position() == doctest::Approx(2.0));
}

TEST_CASE("a click on the waveform seeks") {
    HeadlessImGui imgui;
    ObxUi fx;
    UiContext ui = fx.Make();
    FakeOutput output;
    ui.audio = &output;
    AudioView view(1, Modules::NodePath{{0}}, "a", Ramp(400, 1));
    REQUIRE(view.Duration() == doctest::Approx(4.0));

    REQUIRE(output.Play(Domain::Audio{}).ok());
    Frame(imgui, view, ui, 240.0f, 250.0f, false);
    Frame(imgui, view, ui, 240.0f, 250.0f, false);
    CHECK(output.seeks.empty());
    Frame(imgui, view, ui, 240.0f, 250.0f, true);
    REQUIRE(output.seeks.size() == 1);
    const float padding = ImGui::GetStyle().WindowPadding.x;
    const double fraction = (240.0 - padding) / (480.0 - 2.0 * padding);
    CHECK(output.seeks[0] == doctest::Approx(fraction * 4.0).epsilon(0.02));
    CHECK(view.Position() == doctest::Approx(output.seeks[0]));
    Frame(imgui, view, ui, 240.0f, 250.0f, false);
}
