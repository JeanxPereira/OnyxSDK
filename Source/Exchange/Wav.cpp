#include <Onyx/Core/Services/Paths.h>
#include <Onyx/Exchange/Wav.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace Onyx::Exchange {

namespace {

void PutU16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void PutU32(std::vector<uint8_t>& b, uint32_t v) {
    PutU16(b, v & 0xFFFF);
    PutU16(b, v >> 16);
}

void PutTag(std::vector<uint8_t>& b, const char* tag) {
    b.insert(b.end(), tag, tag + 4);
}

}

Result<void> WriteWav(const Domain::Audio& audio, const std::filesystem::path& out) {
    if (audio.channels == 0 || audio.channels > 0xFFFF || audio.sampleRate == 0) {
        return Result<void>::Fail({"exchange.wav.write", "the audio has no channels or no sample rate", 0});
    }
    const uint64_t dataSize = static_cast<uint64_t>(audio.samples.size()) * 2;
    if (dataSize + 36 > 0xFFFFFFFFull) {
        return Result<void>::Fail({"exchange.wav.write", "the audio is too long for a WAV file", 0});
    }

    std::vector<uint8_t> bytes;
    bytes.reserve(44 + dataSize);
    PutTag(bytes, "RIFF");
    PutU32(bytes, static_cast<uint32_t>(36 + dataSize));
    PutTag(bytes, "WAVE");
    PutTag(bytes, "fmt ");
    PutU32(bytes, 16);
    PutU16(bytes, 1);
    PutU16(bytes, audio.channels);
    PutU32(bytes, audio.sampleRate);
    PutU32(bytes, audio.sampleRate * audio.channels * 2);
    PutU16(bytes, audio.channels * 2);
    PutU16(bytes, 16);
    PutTag(bytes, "data");
    PutU32(bytes, static_cast<uint32_t>(dataSize));
    for (const float sample : audio.samples) {
        const float clamped = std::isnan(sample) ? 0.0f : std::clamp(sample, -1.0f, 1.0f);
        const long scaled = std::lround(clamped * 32768.0f);
        PutU16(bytes, static_cast<uint16_t>(static_cast<int16_t>(std::clamp<long>(scaled, -32768, 32767))));
    }

    std::ofstream file(out, std::ios::binary | std::ios::trunc);
    if (file) file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) return Result<void>::Fail({"exchange.wav.write", "cannot write " + Services::Paths::ToUtf8(out), 0});
    return Result<void>::Ok();
}

}
