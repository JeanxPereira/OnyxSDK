#include <doctest/doctest.h>

#include <Onyx/Media/AudioOutput.h>

#include <chrono>
#include <cmath>
#include <thread>

using namespace Onyx;
using namespace std::chrono_literals;

namespace {

Domain::Audio Tone(double seconds) {
    Domain::Audio audio;
    audio.sampleRate = 44100;
    audio.channels = 2;
    const auto frames = static_cast<size_t>(seconds * audio.sampleRate);
    audio.samples.reserve(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        const auto value = static_cast<float>(0.05 * std::sin(2.0 * 3.14159265358979 * 440.0 * static_cast<double>(i) / 44100.0));
        audio.samples.push_back(value);
        audio.samples.push_back(value);
    }
    return audio;
}

// False (with a MESSAGE) when the runner has no audio device.
bool Started(const Result<void>& played) {
    if (!played.ok() && played.error().code == "media.audio") {
        MESSAGE("media.audio: " << played.error().message << "; skipping the playback checks");
        return false;
    }
    REQUIRE_MESSAGE(played.ok(), played.error().code << ": " << played.error().message);
    return true;
}

}

TEST_CASE("Play a 0.2 s tone, Position advances and Playing drops at the end") {
    Media::AudioOutput output;
    CHECK_FALSE(output.Playing());
    if (!Started(output.Play(Tone(0.2), "test"))) return;
    CHECK(output.Playing());
    CHECK_FALSE(output.Paused());
    CHECK(output.Owner() == "test");

    std::this_thread::sleep_for(100ms);
    CHECK(output.Position() > 0.0);

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (output.Playing() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(5ms);
    CHECK_FALSE(output.Playing());
    CHECK(output.Owner().empty());
    CHECK(output.Position() == doctest::Approx(0.2).epsilon(0.01));
}

TEST_CASE("Pause holds Position, Seek moves it") {
    Media::AudioOutput output;
    if (!Started(output.Play(Tone(2.0), "test"))) return;
    std::this_thread::sleep_for(150ms);
    output.Pause();
    CHECK(output.Paused());
    CHECK(output.Playing());
    const double held = output.Position();
    CHECK(held > 0.0);
    std::this_thread::sleep_for(150ms);
    CHECK(output.Position() == held);

    output.Seek(1.0);
    CHECK(output.Position() == doctest::Approx(1.0));
    output.Seek(10.0);
    CHECK(output.Position() == doctest::Approx(2.0));
    output.Seek(1.0);

    output.Resume();
    CHECK_FALSE(output.Paused());
    std::this_thread::sleep_for(150ms);
    CHECK(output.Position() > 1.0);

    output.Stop();
    CHECK_FALSE(output.Playing());
    CHECK(output.Position() == 0.0);
}

TEST_CASE("AudioOutput rejects a clip without samples") {
    Media::AudioOutput output;
    auto played = output.Play(Domain::Audio{}, "test");
    REQUIRE_FALSE(played.ok());
    CHECK(played.error().code == "media.audio");
    CHECK_FALSE(output.Playing());
    CHECK(output.Owner().empty());
}

TEST_CASE("Stop stops the device") {
    Media::AudioOutput output;
    CHECK_FALSE(output.DeviceRunning());
    if (!Started(output.Play(Tone(1.0), "test"))) return;
    CHECK(output.Playing());
    CHECK(output.DeviceRunning());

    output.Stop();
    CHECK_FALSE(output.Playing());
    CHECK(output.Owner().empty());
    CHECK_FALSE(output.DeviceRunning());

    REQUIRE(output.Play(Tone(1.0), "test").ok());
    CHECK(output.DeviceRunning());
    std::this_thread::sleep_for(100ms);
    CHECK(output.Position() > 0.0);
}

TEST_CASE("Latency is non-negative and below one second") {
    Media::AudioOutput output;
    CHECK(output.Latency() == 0.0);
    if (!Started(output.Play(Tone(0.5), "test"))) return;
    const double latency = output.Latency();
    MESSAGE("device latency " << latency * 1000.0 << " ms");
    CHECK(latency >= 0.0);
    CHECK(latency < 1.0);
}
