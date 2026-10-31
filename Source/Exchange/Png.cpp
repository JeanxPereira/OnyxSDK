// stb_image_write keeps internal linkage here so its symbols never clash with another copy in the final binary.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <Onyx/Exchange/Png.h>

#include "PngEncode.h"

#include <cstdlib>
#include <fstream>

namespace Onyx::Exchange {

std::vector<std::byte> EncodePng(const Domain::Texture& texture) {
    if (texture.format != Domain::PixelFormat::Rgba8 || !texture.IsValid()) return {};
    int length = 0;
    unsigned char* png = stbi_write_png_to_mem(reinterpret_cast<const unsigned char*>(texture.pixels.data()),
                                               static_cast<int>(texture.width * 4), static_cast<int>(texture.width),
                                               static_cast<int>(texture.height), 4, &length);
    if (!png) return {};
    const auto* first = reinterpret_cast<const std::byte*>(png);
    std::vector<std::byte> bytes(first, first + length);
    std::free(png);
    return bytes;
}

Result<void> WritePng(const Domain::Texture& texture, const std::filesystem::path& out) {
    if (texture.format != Domain::PixelFormat::Rgba8 || !texture.IsValid()) {
        return Result<void>::Fail({"exchange.png.format", "only a consistent Rgba8 texture can be written as PNG", 0});
    }
    const std::vector<std::byte> bytes = EncodePng(texture);
    if (bytes.empty()) return Result<void>::Fail({"exchange.png.write", "the PNG encoder failed", 0});
    std::ofstream file(out, std::ios::binary | std::ios::trunc);
    if (file) file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) return Result<void>::Fail({"exchange.png.write", "cannot write " + out.string(), 0});
    return Result<void>::Ok();
}

}
