#include <Onyx/TestKit/Png.h>

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include <cstring>
#include <string>

namespace Onyx::TestKit {

Result<Image8> ReadPng(const std::filesystem::path& path) {
    int w = 0;
    int h = 0;
    int channelsInFile = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &w, &h, &channelsInFile, 4);
    if (!data) {
        const char* reason = stbi_failure_reason();
        return Result<Image8>::Fail({"png.read", "cannot decode " + path.string() + ": " + (reason ? reason : "unknown error"), 0});
    }

    Image8 image;
    image.width = static_cast<uint32_t>(w);
    image.height = static_cast<uint32_t>(h);
    image.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    std::memcpy(image.rgba.data(), data, image.rgba.size());
    stbi_image_free(data);
    return Result<Image8>::Ok(std::move(image));
}

Result<void> WritePng(const std::filesystem::path& path, const Image8& image) {
    const size_t needed = static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4;
    if (image.width == 0 || image.height == 0 || image.rgba.size() < needed) {
        return Result<void>::Fail({"png.write", "image buffer does not cover " + std::to_string(image.width) + "x" +
                                                    std::to_string(image.height) + " RGBA8 for " + path.string(), 0});
    }
    const int ok = stbi_write_png(path.string().c_str(), static_cast<int>(image.width), static_cast<int>(image.height), 4,
                                  image.rgba.data(), static_cast<int>(image.width * 4));
    if (!ok) {
        return Result<void>::Fail({"png.write", "cannot write " + path.string(), 0});
    }
    return Result<void>::Ok();
}

}
