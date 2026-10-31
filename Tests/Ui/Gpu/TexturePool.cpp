#include <doctest/doctest.h>

#include <Onyx/Core/Domain/Texture.h>

#include "Support/UiGpuFixture.h"

using namespace Onyx;

namespace {

Domain::Texture Solid(uint32_t width, uint32_t height, std::byte r, std::byte g, std::byte b) {
    Domain::Texture texture;
    texture.name = "solid";
    texture.width = width;
    texture.height = height;
    texture.format = Domain::PixelFormat::Rgba8;
    for (uint32_t i = 0; i < width * height; ++i) {
        texture.pixels.insert(texture.pixels.end(), {r, g, b, std::byte{255}});
    }
    return texture;
}

}

TEST_CASE("TexturePool creates a texture and reports its size") {
    auto fx = MakeUiGpuFixture();
    const Domain::Texture checker = Solid(4, 4, std::byte{255}, std::byte{0}, std::byte{0});
    auto id = fx->textures->Create(checker);
    REQUIRE_MESSAGE(id.ok(), id.error().code << ": " << id.error().message);
    CHECK(fx->textures->Size(id.value()) == glm::uvec2(4, 4));
    CHECK(fx->textures->Count() == 1);
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("TexturePool rejects a malformed texture") {
    auto fx = MakeUiGpuFixture();
    Domain::Texture empty = Solid(4, 4, std::byte{0}, std::byte{0}, std::byte{0});
    empty.width = 0;
    auto zero = fx->textures->Create(empty);
    REQUIRE_FALSE(zero.ok());
    CHECK(zero.error().code == "ui.texture.size");

    Domain::Texture shortPixels = Solid(4, 4, std::byte{0}, std::byte{0}, std::byte{0});
    shortPixels.pixels.resize(8);
    auto cut = fx->textures->Create(shortPixels);
    REQUIRE_FALSE(cut.ok());
    CHECK(cut.error().code == "ui.texture.size");
    CHECK(fx->textures->Count() == 0);
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("TexturePool Update keeps the id and rejects a different size") {
    auto fx = MakeUiGpuFixture();
    auto id = fx->textures->Create(Solid(4, 4, std::byte{255}, std::byte{0}, std::byte{0}));
    REQUIRE(id.ok());

    auto other = fx->textures->Update(id.value(), Solid(8, 8, std::byte{0}, std::byte{0}, std::byte{0}));
    REQUIRE_FALSE(other.ok());
    CHECK(other.error().code == "ui.texture.size");

    auto same = fx->textures->Update(id.value(), Solid(4, 4, std::byte{0}, std::byte{255}, std::byte{0}));
    CHECK_MESSAGE(same.ok(), same.error().code << ": " << same.error().message);
    CHECK(fx->textures->Size(id.value()) == glm::uvec2(4, 4));
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("TexturePool Remove retires the image and the descriptor") {
    auto fx = MakeUiGpuFixture();
    auto id = fx->textures->Create(Solid(4, 4, std::byte{255}, std::byte{0}, std::byte{0}));
    REQUIRE(id.ok());
    fx->textures->Remove(id.value());
    CHECK(fx->textures->Count() == 0);
    CHECK(fx->device->Retire().Pending() > 0);
    CHECK(fx->textures->Size(id.value()) == glm::uvec2(0, 0));
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("TexturePool Register wraps an external view without owning it") {
    auto fx = MakeUiGpuFixture();
    Gpu::RenderTargetDesc desc;
    desc.extent = {16, 16};
    desc.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    desc.depthFormat = VK_FORMAT_UNDEFINED;
    desc.samples = VK_SAMPLE_COUNT_1_BIT;
    auto target = Gpu::RenderTarget::Create(*fx->device, desc);
    REQUIRE(target.ok());

    auto id = fx->textures->Register(target.value()->ResolveView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    REQUIRE_MESSAGE(id.ok(), id.error().code << ": " << id.error().message);
    CHECK(fx->textures->Count() == 1);
    CHECK(fx->textures->Size(id.value()) == glm::uvec2(0, 0));
    fx->textures->Remove(id.value());
    CHECK(fx->textures->Count() == 0);
    CHECK(fx->device->ValidationMessageCount() == 0);
}
