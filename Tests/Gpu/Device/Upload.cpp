#include <doctest/doctest.h>

#include <vector>

#include "Onyx/Core/Domain/Texture.h"
#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/Handles.h"
#include "Onyx/Gpu/Upload.h"
#include "Support/TestDevice.h"

using namespace Onyx;

namespace {

std::vector<std::byte> Pattern(size_t count, size_t seed = 7) {
    std::vector<std::byte> bytes(count);
    for (size_t i = 0; i < count; ++i) {
        bytes[i] = static_cast<std::byte>((i * seed) & 0xFF);
    }
    return bytes;
}

Domain::Texture Rgba(uint32_t w, uint32_t h) {
    Domain::Texture t;
    t.name = "rgba";
    t.width = w;
    t.height = h;
    t.format = Domain::PixelFormat::Rgba8;
    t.pixels = Pattern(size_t(w) * h * 4, 3);
    return t;
}

}

TEST_CASE("UploadContext uploads bytes and Download returns them after one flush") {
    auto device = CreateTestDevice();
    auto up = Gpu::UploadContext::Create(*device);
    REQUIRE(up.ok());
    auto src = Pattern(4096);
    auto buf = up.value()->Upload(src, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    REQUIRE(buf.ok());
    auto before = device->SubmitCount();
    auto v = up.value()->Flush();
    CHECK(device->SubmitCount() == before + 1);
    CHECK(up.value()->Flush() == v);
    CHECK(up.value()->LastFlushed() == v);
    auto back = up.value()->Download(buf.value(), 4096);
    REQUIRE(back.ok());
    CHECK(back.value() == src);
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("Ten buffers and three textures flush in exactly one submit") {
    auto device = CreateTestDevice();
    auto up = Gpu::UploadContext::Create(*device);
    REQUIRE(up.ok());
    std::vector<Gpu::Buffer> buffers;
    for (size_t i = 0; i < 10; ++i) {
        auto b = up.value()->Upload(Pattern(1000 + i * 17, i + 1), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        REQUIRE(b.ok());
        buffers.push_back(std::move(b.value()));
    }
    std::vector<Gpu::Image> images;
    for (uint32_t size : {16u, 33u, 64u}) {
        auto img = up.value()->Upload(Rgba(size, size));
        REQUIRE(img.ok());
        CHECK(img.value().mipLevels > 1);
        CHECK(img.value().format == VK_FORMAT_R8G8B8A8_UNORM);
        images.push_back(std::move(img.value()));
    }
    CHECK(device->SubmitCount() == 0);
    up.value()->Flush();
    CHECK(device->SubmitCount() == 1);
    CHECK(images[2].mipLevels == 7);
    auto back = up.value()->Download(buffers[3], buffers[3].size);
    REQUIRE(back.ok());
    CHECK(back.value() == Pattern(1000 + 3 * 17, 4));
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("UploadContext refuses a texture whose pixel vector is short") {
    auto device = CreateTestDevice();
    auto up = Gpu::UploadContext::Create(*device);
    REQUIRE(up.ok());
    Domain::Texture t;
    t.width = 4;
    t.height = 4;
    t.format = Domain::PixelFormat::Rgba8;
    t.pixels.resize(10);
    auto img = up.value()->Upload(t);
    REQUIRE_FALSE(img.ok());
    CHECK(img.error().code == "gpu.upload.size");
    CHECK(device->SubmitCount() == 0);
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("A block-compressed texture uploads with one mip") {
    auto device = CreateTestDevice();
    auto up = Gpu::UploadContext::Create(*device);
    REQUIRE(up.ok());
    Domain::Texture t;
    t.width = 8;
    t.height = 8;
    t.format = Domain::PixelFormat::Bc1;
    t.pixels = Pattern(32);
    auto img = up.value()->Upload(t);
    if (!img.ok() && img.error().code == "gpu.upload.format") {
        MESSAGE("BC1 sampling is not supported on this device");
        return;
    }
    REQUIRE(img.ok());
    CHECK(img.value().mipLevels == 1);
    CHECK(img.value().format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
    up.value()->Flush();
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("A half-float texture uploads with one mip") {
    auto device = CreateTestDevice();
    auto up = Gpu::UploadContext::Create(*device);
    REQUIRE(up.ok());
    Domain::Texture t;
    t.width = 4;
    t.height = 2;
    t.format = Domain::PixelFormat::RgbaF16;
    t.pixels = Pattern(4 * 2 * 8);
    auto img = up.value()->Upload(t);
    REQUIRE(img.ok());
    CHECK(img.value().mipLevels == 1);
    CHECK(img.value().format == VK_FORMAT_R16G16B16A16_SFLOAT);
    up.value()->Flush();
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("Staging exhaustion flushes mid-way instead of failing") {
    auto device = CreateTestDevice();
    auto up = Gpu::UploadContext::Create(*device, {.stagingBytes = 1u << 20});
    REQUIRE(up.ok());
    std::vector<Gpu::Buffer> buffers;
    std::vector<std::vector<std::byte>> sources;
    for (size_t i = 0; i < 3; ++i) {
        sources.push_back(Pattern(512 * 1024, i + 3));
        auto b = up.value()->Upload(sources.back(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        REQUIRE(b.ok());
        buffers.push_back(std::move(b.value()));
    }
    CHECK(device->SubmitCount() >= 1);
    auto back = up.value()->Download(buffers[2], buffers[2].size);
    REQUIRE(back.ok());
    CHECK(device->SubmitCount() >= 2);
    CHECK(back.value() == sources[2]);
    auto first = up.value()->Download(buffers[0], buffers[0].size);
    REQUIRE(first.ok());
    CHECK(first.value() == sources[0]);
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("A request larger than the staging ring still uploads") {
    auto device = CreateTestDevice();
    auto up = Gpu::UploadContext::Create(*device, {.stagingBytes = 4096});
    REQUIRE(up.ok());
    auto src = Pattern(64 * 1024, 5);
    auto buf = up.value()->Upload(src, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    REQUIRE(buf.ok());
    auto back = up.value()->Download(buf.value(), src.size());
    REQUIRE(back.ok());
    CHECK(back.value() == src);
    CHECK(device->ValidationMessageCount() == 0);
}
