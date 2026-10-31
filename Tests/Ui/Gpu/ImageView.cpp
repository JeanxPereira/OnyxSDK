#include <doctest/doctest.h>

#include "../Support/ObxWorkspace.h"

#include <Onyx/Gpu/RenderTarget.h>
#include <Onyx/Ui/Viewers/ImageView.h>
#include <Onyx/Ui/Viewers/Opening.h>

#include "Support/UiGpuFixture.h"

#include <memory>

using namespace Onyx;

namespace {

Domain::Texture Solid(uint32_t size, std::byte r, std::byte g, std::byte b) {
    Domain::Texture texture;
    texture.name = "solid";
    texture.width = size;
    texture.height = size;
    for (uint32_t i = 0; i < size * size; ++i) texture.pixels.insert(texture.pixels.end(), {r, g, b, std::byte{255}});
    return texture;
}

size_t CountReddish(const TestKit::Image8& image) {
    size_t count = 0;
    for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        if (std::to_integer<int>(image.rgba[i]) > 200 && std::to_integer<int>(image.rgba[i + 1]) < 60 &&
            std::to_integer<int>(image.rgba[i + 2]) < 60) {
            ++count;
        }
    }
    return count;
}

std::unique_ptr<Gpu::RenderTarget> MakeTarget(Gpu::Device& device) {
    Gpu::RenderTargetDesc desc;
    desc.extent = {256, 256};
    desc.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    desc.depthFormat = VK_FORMAT_UNDEFINED;
    desc.samples = VK_SAMPLE_COUNT_1_BIT;
    auto target = Gpu::RenderTarget::Create(device, desc);
    REQUIRE_MESSAGE(target.ok(), target.error().code << ": " << target.error().message);
    return std::move(target.value());
}

Result<TestKit::Image8> DrawView(UiGpuFixture& fx, Gpu::RenderTarget& target, Ui::IPanel& view, Ui::UiContext& ui) {
    return RenderImGuiFrame(fx, target, [&] {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(256, 256));
        ImGui::Begin("viewer");
        view.Draw(ui);
        ImGui::End();
    });
}

}

TEST_CASE("ImageView uploads on its first draw, shows the pixels and removes the texture on destruction") {
    auto fx = MakeUiGpuFixture();
    ObxUi obx;
    Ui::UiContext ui = obx.Make();
    ui.textures = fx->textures.get();
    auto target = MakeTarget(*fx->device);

    {
        Ui::ImageView view(1, Modules::NodePath{{0}}, "red", Solid(16, std::byte{255}, std::byte{0}, std::byte{0}));
        CHECK_FALSE(view.Uploaded());

        size_t red = 0;
        for (int frame = 0; frame < 3; ++frame) {
            auto image = DrawView(*fx, *target, view, ui);
            REQUIRE_MESSAGE(image.ok(), image.error().code << ": " << image.error().message);
            red = CountReddish(image.value());
        }
        CHECK(view.Uploaded());
        CHECK_FALSE(view.Fault().has_value());
        CHECK(fx->textures->Count() == 1);
        CHECK(red > 1000);
    }
    CHECK(fx->textures->Count() == 0);
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("ImageView alpha mode adds a texture and frees it with the view") {
    auto fx = MakeUiGpuFixture();
    ObxUi obx;
    Ui::UiContext ui = obx.Make();
    ui.textures = fx->textures.get();
    auto target = MakeTarget(*fx->device);

    {
        Ui::ImageView view(1, Modules::NodePath{{0}}, "red", Solid(16, std::byte{255}, std::byte{0}, std::byte{0}));
        for (int frame = 0; frame < 2; ++frame) REQUIRE(DrawView(*fx, *target, view, ui).ok());
        view.ShowAlpha(true);
        for (int frame = 0; frame < 3; ++frame) REQUIRE(DrawView(*fx, *target, view, ui).ok());
        CHECK(fx->textures->Count() == 2);
        view.ShowAlpha(false);
        for (int frame = 0; frame < 2; ++frame) REQUIRE(DrawView(*fx, *target, view, ui).ok());
    }
    CHECK(fx->textures->Count() == 0);
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("ImageView keeps the pool's code when the upload fails") {
    auto fx = MakeUiGpuFixture();
    ObxUi obx;
    Ui::UiContext ui = obx.Make();
    ui.textures = fx->textures.get();
    auto target = MakeTarget(*fx->device);

    Domain::Texture broken = Solid(16, std::byte{0}, std::byte{0}, std::byte{0});
    broken.pixels.resize(8);
    Ui::ImageView view(1, Modules::NodePath{{0}}, "broken", std::move(broken));
    for (int frame = 0; frame < 2; ++frame) REQUIRE(DrawView(*fx, *target, view, ui).ok());
    REQUIRE(view.Fault().has_value());
    CHECK(view.Fault()->code == "ui.texture.size");
    CHECK_FALSE(view.Uploaded());
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("OpenEntry on the image entry decodes on the job lane and places an ImageView after Pump") {
    auto fx = MakeUiGpuFixture();
    ObxUi obx;
    Ui::UiContext ui = obx.Make();
    ui.textures = fx->textures.get();
    auto target = MakeTarget(*fx->device);

    std::unique_ptr<Ui::IDocumentView> placed;
    const auto opened = Ui::OpenEntry(ui, obx.Pick(ObxWorkspace::kImage),
                                      [&](std::unique_ptr<Ui::IDocumentView> v) { placed = std::move(v); });
    REQUIRE_MESSAGE(opened.ok(), opened.error().code << ": " << opened.error().message);
    CHECK(placed == nullptr);
    REQUIRE(obx.PumpUntil([&] { return placed != nullptr; }));
    auto* view = dynamic_cast<Ui::ImageView*>(placed.get());
    REQUIRE(view != nullptr);

    for (int frame = 0; frame < 3; ++frame) REQUIRE(DrawView(*fx, *target, *view, ui).ok());
    CHECK(view->Uploaded());
    CHECK(fx->textures->Count() == 1);
    placed.reset();
    CHECK(fx->textures->Count() == 0);
    CHECK(fx->device->ValidationMessageCount() == 0);
}
