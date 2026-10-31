#pragma once

#include <Onyx/Core/Result.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace Onyx::TestKit {

// A tightly packed top-down RGBA8 image.
struct Image8 {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::byte> rgba;
};

// Decodes any PNG the decoder accepts, always to four channels.
Result<Image8> ReadPng(const std::filesystem::path& path);

// Encodes RGBA8 with a stride of width * 4; fails when rgba is shorter than width * height * 4.
Result<void> WritePng(const std::filesystem::path& path, const Image8& image);

}
