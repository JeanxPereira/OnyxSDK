#pragma once

#include <Onyx/Core/Domain/Audio.h>
#include <Onyx/Core/Result.h>

#include <filesystem>

namespace Onyx::Exchange {

// RIFF/WAVE, PCM 16-bit little-endian, interleaved. Floats are clamped to [-1, 1] and rounded.
// Error: exchange.wav.write.
Result<void> WriteWav(const Domain::Audio& audio, const std::filesystem::path& out);

}
