#include <doctest/doctest.h>

#include <Onyx/Exchange/Wav.h>

#include "Support.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Onyx;

namespace {

uint32_t U16(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8);
}

uint32_t U32(const std::vector<uint8_t>& b, size_t at) {
    return U16(b, at) | (U16(b, at + 2) << 16);
}

std::string Tag(const std::vector<uint8_t>& b, size_t at) {
    return std::string(b.begin() + static_cast<std::ptrdiff_t>(at), b.begin() + static_cast<std::ptrdiff_t>(at) + 4);
}

}

TEST_CASE("WriteWav writes a 44-byte header and the samples") {
    Tests::TempDir dir("wav");
    Domain::Audio audio;
    audio.sampleRate = 8000;
    audio.channels = 2;
    audio.samples = {-1.0f, 1.0f, 0.0f, 0.5f, -2.0f, 2.0f, 0.25f, -0.25f};
    const auto path = dir.path / "tone.wav";

    REQUIRE(Exchange::WriteWav(audio, path).ok());

    std::ifstream file(path, std::ios::binary);
    const std::vector<uint8_t> b((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(b.size() == 44 + 16);
    CHECK(Tag(b, 0) == "RIFF");
    CHECK(U32(b, 4) == 36 + 16);
    CHECK(Tag(b, 8) == "WAVE");
    CHECK(Tag(b, 12) == "fmt ");
    CHECK(U32(b, 16) == 16);
    CHECK(U16(b, 20) == 1);
    CHECK(U16(b, 22) == 2);
    CHECK(U32(b, 24) == 8000);
    CHECK(U32(b, 28) == 8000 * 2 * 2);
    CHECK(U16(b, 32) == 4);
    CHECK(U16(b, 34) == 16);
    CHECK(Tag(b, 36) == "data");
    CHECK(U32(b, 40) == 16);
    CHECK(U16(b, 44) == 0x8000);
    CHECK(U16(b, 46) == 0x7FFF);
    CHECK(U16(b, 48) == 0);
    CHECK(U16(b, 50) == 0x4000);
    CHECK(U16(b, 52) == 0x8000);
    CHECK(U16(b, 54) == 0x7FFF);
}

TEST_CASE("WriteWav refuses audio without channels") {
    Tests::TempDir dir("wav_empty");
    const Result<void> r = Exchange::WriteWav(Domain::Audio{}, dir.path / "none.wav");
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().code == "exchange.wav.write");
}

TEST_CASE("WriteWav reports exchange.wav.write for an unwritable path") {
    Tests::TempDir dir("wav_unwritable");
    const auto blocker = dir.path / "blocker";
    { std::ofstream(blocker) << "x"; }
    Domain::Audio audio;
    audio.sampleRate = 8000;
    audio.channels = 1;
    audio.samples = {0.0f, 0.5f};
    const Result<void> written = Exchange::WriteWav(audio, blocker / "x.wav");
    REQUIRE_FALSE(written.ok());
    CHECK(written.error().code == "exchange.wav.write");
}
