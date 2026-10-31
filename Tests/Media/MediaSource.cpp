#include <doctest/doctest.h>

#include <Onyx/Core/Vfs/MemoryFile.h>
#include <Onyx/Core/Vfs/OsFile.h>
#include <Onyx/Media/MediaSource.h>

#include "Support/ClipFixture.h"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <random>
#include <thread>
#include <vector>

using namespace Onyx;
using namespace std::chrono_literals;

namespace {

using Clock = std::chrono::steady_clock;

std::shared_ptr<const Vfs::IFile> OpenFile(const ClipFixture& clip) {
    auto file = Vfs::OsFile::Open(clip.path);
    REQUIRE_MESSAGE(file.ok(), file.error().code << ": " << file.error().message);
    return file.value();
}

void OpenClip(Media::MediaSource& source, const ClipFixture& clip) {
    auto info = source.Open(OpenFile(clip), clip.offset, clip.size);
    REQUIRE_MESSAGE(info.ok(), info.error().code << ": " << info.error().message);
    source.SetVolume(0.0f);
}

// Polls TakeFrame until a frame arrives or `timeout` passes.
std::optional<Media::VideoFrame> NextFrame(Media::MediaSource& source, std::chrono::milliseconds timeout = 3000ms) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
        if (auto frame = source.TakeFrame()) return frame;
        std::this_thread::sleep_for(1ms);
    }
    return std::nullopt;
}

bool Near(const std::array<uint8_t, 3>& got, const std::array<uint8_t, 3>& want, int tolerance = 12) {
    for (size_t c = 0; c < 3; ++c) {
        if (std::abs(static_cast<int>(got[c]) - static_cast<int>(want[c])) > tolerance) return false;
    }
    return true;
}

std::string Text(const std::array<uint8_t, 3>& c) {
    return std::to_string(c[0]) + "," + std::to_string(c[1]) + "," + std::to_string(c[2]);
}

}

TEST_CASE("Open reads a clip at an offset and reports its info") {
    for (const bool withAudio : {true, false}) {
        CAPTURE(withAudio);
        const ClipFixture clip = WriteClip(withAudio ? "info-av" : "info-v", withAudio);
        CHECK(clip.offset == 4096);
        Media::MediaSource source;
        OpenClip(source, clip);
        const Media::MediaInfo& info = source.Info();
        CHECK(std::abs(info.duration - 2.0) <= 0.1);
        CHECK(info.width == 64);
        CHECK(info.height == 48);
        CHECK(info.fps == doctest::Approx(25.0));
        CHECK(info.displayAspect == doctest::Approx(64.0 / 48.0));
        CHECK(info.hasVideo);
        CHECK(info.hasAudio == withAudio);
        CHECK(info.videoCodec == "mpeg1video");
        CHECK(info.audioCodec == (withAudio ? "pcm_s16le" : ""));
        CHECK(source.State() == Media::PlayState::Stopped);
        CHECK(source.Position() == 0.0);
    }
}

TEST_CASE("TakeFrame after Play yields frames in order with the expected colours") {
    const ClipFixture clip = WriteClip("order", true);
    Media::MediaSource source;
    OpenClip(source, clip);
    source.Play();
    CHECK(source.State() == Media::PlayState::Playing);

    auto first = NextFrame(source);
    REQUIRE(first.has_value());
    CHECK(first->width == 64);
    CHECK(first->height == 48);
    CHECK(first->rgba.size() == 64u * 48u * 4u);
    CHECK_MESSAGE(Near(CentreColour(*first), Expected(0)), Text(CentreColour(*first)));

    auto later = NextFrame(source);
    REQUIRE(later.has_value());
    CHECK(later->pts > first->pts);
    const auto index = static_cast<uint32_t>(std::lround(later->pts * 25.0));
    CHECK_MESSAGE(Near(CentreColour(*later), Expected(index)), Text(CentreColour(*later)) << " at frame " << index);
}

TEST_CASE("Seek lands on the right frame") {
    const ClipFixture clip = WriteClip("seek", true);
    Media::MediaSource source;
    OpenClip(source, clip);

    SUBCASE("while stopped") {
        source.Seek(1.0);
        CHECK(source.Position() == doctest::Approx(1.0));
        auto frame = NextFrame(source);
        REQUIRE(frame.has_value());
        CHECK(std::abs(frame->pts - 1.0) <= 0.08);
        CHECK_MESSAGE(Near(CentreColour(*frame), Expected(25)), Text(CentreColour(*frame)));
    }
    SUBCASE("while playing") {
        source.Play();
        REQUIRE(NextFrame(source).has_value());
        std::this_thread::sleep_for(200ms);
        source.Seek(1.0);
        auto frame = NextFrame(source);
        REQUIRE(frame.has_value());
        CHECK(std::abs(frame->pts - 1.0) <= 0.08);
        CHECK_MESSAGE(Near(CentreColour(*frame), Expected(25)), Text(CentreColour(*frame)));
    }
}

TEST_CASE("Pause freezes Position and Resume continues") {
    const ClipFixture clip = WriteClip("pause", true);
    Media::MediaSource source;
    OpenClip(source, clip);
    source.Play();
    std::this_thread::sleep_for(400ms);
    source.Pause();
    CHECK(source.State() == Media::PlayState::Paused);
    std::this_thread::sleep_for(50ms);
    const double held = source.Position();
    CHECK(held > 0.1);
    std::this_thread::sleep_for(250ms);
    CHECK(source.Position() == doctest::Approx(held));

    source.Play();
    std::this_thread::sleep_for(400ms);
    CHECK(source.Position() > held + 0.15);
}

TEST_CASE("Stop then Play restarts from zero") {
    const ClipFixture clip = WriteClip("stop", true);
    Media::MediaSource source;
    OpenClip(source, clip);
    source.Play();
    std::this_thread::sleep_for(400ms);
    while (source.TakeFrame()) {
    }
    source.Stop();
    CHECK(source.State() == Media::PlayState::Stopped);
    CHECK(source.Position() == 0.0);

    source.Play();
    auto frame = NextFrame(source);
    REQUIRE(frame.has_value());
    CHECK(frame->pts < 0.1);
    CHECK_MESSAGE(Near(CentreColour(*frame), Expected(0)), Text(CentreColour(*frame)));
}

TEST_CASE("Play after the end restarts from zero") {
    const ClipFixture clip = WriteClip("end", false, 10);
    Media::MediaSource source;
    OpenClip(source, clip);
    source.Play();
    const auto deadline = Clock::now() + 3s;
    while (source.State() != Media::PlayState::Ended && Clock::now() < deadline) {
        source.TakeFrame();
        std::this_thread::sleep_for(5ms);
    }
    REQUIRE(source.State() == Media::PlayState::Ended);
    CHECK(source.Position() == doctest::Approx(source.Info().duration));

    source.Play();
    CHECK(source.State() == Media::PlayState::Playing);
    auto frame = NextFrame(source);
    REQUIRE(frame.has_value());
    CHECK(frame->pts < 0.1);
    CHECK(Near(CentreColour(*frame), Expected(0)));
}

namespace {

struct PlayedOut {
    bool ended = false;
    bool monotonic = true;
    double seconds = 0;
    double finalPosition = 0;
    std::optional<Media::VideoFrame> last;
};

// Plays the clip until Ended or `timeout`, taking every frame and watching Position.
PlayedOut PlayToEnd(Media::MediaSource& source, std::chrono::milliseconds timeout) {
    PlayedOut out;
    const auto start = Clock::now();
    double previous = 0;
    source.Play();
    while (Clock::now() - start < timeout) {
        if (auto frame = source.TakeFrame()) out.last = std::move(frame);
        const double position = source.Position();
        if (position + 1e-9 < previous) out.monotonic = false;
        previous = position;
        if (source.State() == Media::PlayState::Ended) {
            out.ended = true;
            break;
        }
        std::this_thread::sleep_for(2ms);
    }
    out.seconds = std::chrono::duration<double>(Clock::now() - start).count();
    out.finalPosition = source.Position();
    return out;
}

}

TEST_CASE("a clip whose audio ends first plays its video tail to the end") {
    const ClipFixture clip = WriteClip("short-audio", true, 50, 1.0);
    Media::MediaSource source;
    OpenClip(source, clip);
    REQUIRE(source.Info().hasAudio);
    const PlayedOut played = PlayToEnd(source, 5000ms);
    REQUIRE(played.ended);
    CHECK(played.monotonic);
    CHECK(played.seconds > 1.7);
    CHECK(std::abs(played.finalPosition - 2.0) <= 0.1);
    REQUIRE(played.last.has_value());
    const auto index = static_cast<uint32_t>(std::lround(played.last->pts * 25.0));
    CHECK(index >= 45);
    CHECK_MESSAGE(Near(CentreColour(*played.last), Expected(index)), Text(CentreColour(*played.last)) << " at frame " << index);
}

TEST_CASE("a long video tail after one second of audio still reaches Ended") {
    const ClipFixture clip = WriteClip("long-tail", true, 100, 1.0);
    Media::MediaSource source;
    OpenClip(source, clip);
    const PlayedOut played = PlayToEnd(source, 7000ms);
    REQUIRE(played.ended);
    CHECK(played.monotonic);
    CHECK(played.seconds > 3.7);
    CHECK(std::abs(played.finalPosition - 4.0) <= 0.1);
    REQUIRE(played.last.has_value());
    CHECK(played.last->pts > 3.8);
}

TEST_CASE("audio stored ten frames behind the video keeps the audio clock") {
    const ClipFixture clip = WriteClip("audio-lag", true, 50, 0, 10);
    Media::MediaSource source;
    OpenClip(source, clip);
    REQUIRE(source.Info().hasAudio);
    if (!source.UsesAudioClock()) {
        MESSAGE("media.audio: no playback device, the clip runs on the steady clock; skipping");
        return;
    }

    source.Play();
    const auto start = Clock::now();
    bool audioClockThroughout = true;
    double position = 0;
    while (Clock::now() - start < 1200ms) {
        source.TakeFrame();
        audioClockThroughout = audioClockThroughout && source.UsesAudioClock();
        position = source.Position();
        std::this_thread::sleep_for(2ms);
    }
    CHECK(audioClockThroughout);
    CHECK(position > 1.0);

    const PlayedOut rest = PlayToEnd(source, 3000ms);
    REQUIRE(rest.ended);
    CHECK(std::abs(rest.finalPosition - 2.0) <= 0.1);
}

TEST_CASE("Ten Open/destroy cycles each return within one second") {
    const ClipFixture clip = WriteClip("cycles", true);
    auto file = OpenFile(clip);
    double slowest = 0;
    std::string times;
    for (int i = 0; i < 10; ++i) {
        auto source = std::make_unique<Media::MediaSource>();
        auto info = source->Open(file, clip.offset, clip.size);
        REQUIRE(info.ok());
        source->SetVolume(0.0f);
        source->Play();
        std::this_thread::sleep_for(30ms);
        const auto start = Clock::now();
        source.reset();
        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
        CHECK(seconds < 1.0);
        slowest = std::max(slowest, seconds);
        times += std::to_string(seconds * 1000.0).substr(0, 5) + "ms ";
    }
    MESSAGE("destructor times: " << times << "(slowest " << slowest * 1000.0 << " ms)");
}

TEST_CASE("Open fails with media.open on garbage") {
    std::mt19937 random(1234);
    std::vector<std::byte> bytes(64 * 1024);
    for (std::byte& b : bytes) b = static_cast<std::byte>(random() & 0xFF);
    auto file = std::make_shared<Vfs::MemoryFile>(std::move(bytes));

    Media::MediaSource source;
    auto info = source.Open(file, 0, 0);
    REQUIRE_FALSE(info.ok());
    CHECK(info.error().code == "media.open");

    auto outside = source.Open(file, 60 * 1024, 8 * 1024);
    REQUIRE_FALSE(outside.ok());
    CHECK(outside.error().code == "media.open");
}

TEST_CASE("the video-only clip keeps time on the steady clock") {
    const ClipFixture clip = WriteClip("steady", false);
    Media::MediaSource source;
    OpenClip(source, clip);
    source.Play();
    const auto start = Clock::now();
    const double from = source.Position();
    std::this_thread::sleep_for(500ms);
    const double advanced = source.Position() - from;
    const double wall = std::chrono::duration<double>(Clock::now() - start).count();
    CHECK(std::abs(advanced - wall) <= 0.1);
    CHECK(advanced > 0.4);
}
