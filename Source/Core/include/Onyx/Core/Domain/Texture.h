#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Onyx::Domain {

enum class PixelFormat : uint8_t { Rgba8, Bc1, Bc3, Bc4, Bc5, Bc7, RgbaF16 };

bool IsBlockCompressed(PixelFormat format);
size_t BytesPerBlock(PixelFormat format);
size_t ExpectedByteSize(PixelFormat format, uint32_t width, uint32_t height);

struct Texture {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    PixelFormat format = PixelFormat::Rgba8;
    std::vector<std::byte> pixels;

    bool IsValid() const;
};

}
