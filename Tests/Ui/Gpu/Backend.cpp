#include <doctest/doctest.h>

#include <Onyx/Core/Domain/Texture.h>

#include <cstdlib>

#include "Support/UiGpuFixture.h"

using namespace Onyx;

namespace {

constexpr uint32_t kSize = 128;

std::unique_ptr<Gpu::RenderTarget> MakeTarget(Gpu::Device& device) {
    Gpu::RenderTargetDesc desc;
    desc.extent = {kSize, kSize};
    desc.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    desc.depthFormat = VK_FORMAT_UNDEFINED;
    desc.samples = VK_SAMPLE_COUNT_1_BIT;
    auto target = Gpu::RenderTarget::Create(device, desc);
    REQUIRE_MESSAGE(target.ok(), target.error().code << ": " << target.error().message);
    return std::move(target.value());
}

// 4x4 texels alternating red and blue.
Domain::Texture Checker() {
    Domain::Texture texture;
    texture.name = "checker";
    texture.width = 4;
    texture.height = 4;
    texture.format = Domain::PixelFormat::Rgba8;
    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            const bool red = ((x + y) & 1u) == 0;
            texture.pixels.insert(texture.pixels.end(),
                                  {std::byte{red ? uint8_t{255} : uint8_t{0}}, std::byte{0},
                                   std::byte{red ? uint8_t{0} : uint8_t{255}}, std::byte{255}});
        }
    }
    return texture;
}

void Plate(const char* id, const std::function<void()>& content) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(kSize, kSize));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
    content();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

int Channel(const TestKit::Image8& image, uint32_t x, uint32_t y, uint32_t channel) {
    return std::to_integer<int>(image.rgba[(static_cast<size_t>(y) * image.width + x) * 4 + channel]);
}

}

TEST_CASE("Backend renders a pooled texture into the target") {
    auto fx = MakeUiGpuFixture();
    auto target = MakeTarget(*fx->device);
    auto id = fx->textures->Create(Checker());
    REQUIRE(id.ok());

    auto image = RenderImGuiFrame(*fx, *target, [&] {
        Plate("plate", [&] { ImGui::Image(id.value(), ImVec2(64, 64)); });
    });
    REQUIRE_MESSAGE(image.ok(), image.error().code << ": " << image.error().message);

    // Each texel covers 16x16 pixels of the 64x64 image; texel (0,0) is red and (1,0) is blue.
    const TestKit::Image8& px = image.value();
    CHECK(Channel(px, 8, 8, 0) > 200);
    CHECK(Channel(px, 8, 8, 2) < 50);
    CHECK(Channel(px, 24, 8, 0) < 50);
    CHECK(Channel(px, 24, 8, 2) > 200);
    // Outside the 64x64 image the clear colour remains.
    CHECK(Channel(px, 100, 100, 0) == 0);
    CHECK(Channel(px, 100, 100, 2) == 0);
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("Backend shows the pixels of an updated texture") {
    auto fx = MakeUiGpuFixture();
    auto target = MakeTarget(*fx->device);
    auto id = fx->textures->Create(Checker());
    REQUIRE(id.ok());
    auto first = RenderImGuiFrame(*fx, *target, [&] {
        Plate("plate", [&] { ImGui::Image(id.value(), ImVec2(64, 64)); });
    });
    REQUIRE(first.ok());

    Domain::Texture green = Checker();
    for (size_t i = 0; i < green.pixels.size(); i += 4) {
        green.pixels[i] = std::byte{0};
        green.pixels[i + 1] = std::byte{255};
        green.pixels[i + 2] = std::byte{0};
    }
    auto updated = fx->textures->Update(id.value(), green);
    REQUIRE_MESSAGE(updated.ok(), updated.error().code << ": " << updated.error().message);

    auto second = RenderImGuiFrame(*fx, *target, [&] {
        Plate("plate", [&] { ImGui::Image(id.value(), ImVec2(64, 64)); });
    });
    REQUIRE(second.ok());
    CHECK(Channel(second.value(), 8, 8, 1) > 200);
    CHECK(Channel(second.value(), 8, 8, 0) < 50);
    CHECK(Channel(second.value(), 24, 8, 1) > 200);
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("Backend leaves the clear colour when nothing is drawn") {
    auto fx = MakeUiGpuFixture();
    auto target = MakeTarget(*fx->device);
    const glm::vec4 clear(0.25f, 0.5f, 0.75f, 1.0f);
    auto image = RenderImGuiFrame(*fx, *target, [] {}, clear);
    REQUIRE_MESSAGE(image.ok(), image.error().code << ": " << image.error().message);
    for (uint32_t y = 0; y < kSize; ++y) {
        for (uint32_t x = 0; x < kSize; ++x) {
            const int dr = std::abs(Channel(image.value(), x, y, 0) - 64);
            const int dg = std::abs(Channel(image.value(), x, y, 1) - 128);
            const int db = std::abs(Channel(image.value(), x, y, 2) - 191);
            if (dr > 2 || dg > 2 || db > 2) {
                FAIL("pixel (" << x << "," << y << ") differs from the clear colour");
            }
        }
    }
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("Backend draws text and survives RebuildFonts") {
    auto fx = MakeUiGpuFixture();
    auto target = MakeTarget(*fx->device);
    ImGui::GetIO().Fonts->Clear();
    ImGui::GetIO().Fonts->AddFontDefault();

    auto text = [] { Plate("plate", [] { ImGui::TextUnformatted("Onyx 0123"); }); };
    auto before = RenderImGuiFrame(*fx, *target, text);
    REQUIRE_MESSAGE(before.ok(), before.error().code << ": " << before.error().message);

    fx->backend->RebuildFonts();
    auto after = RenderImGuiFrame(*fx, *target, text);
    REQUIRE_MESSAGE(after.ok(), after.error().code << ": " << after.error().message);

    // The glyphs are drawn: some pixel of the first text row differs from the clear colour.
    bool lit = false;
    for (uint32_t y = 0; y < 16 && !lit; ++y) {
        for (uint32_t x = 0; x < kSize && !lit; ++x) {
            lit = Channel(after.value(), x, y, 0) > 0;
        }
    }
    CHECK(lit);
    CHECK(before.value().rgba == after.value().rgba);
    CHECK(fx->device->ValidationMessageCount() == 0);
}
