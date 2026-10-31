#include <doctest/doctest.h>

#include <Onyx/Render/RenderToImage.h>
#include <Onyx/TestKit/ImageCompare.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cstddef>
#include <span>

#include "Support/RenderFixture.h"

using namespace Onyx;

namespace {

Domain::Texture RenderOrFail(Gpu::Device& device, const Domain::Scene& scene, const Render::View& view,
                             const Render::Overlays& overlays, uint32_t size) {
    auto image = Render::RenderToImage(device, scene, view, overlays, {size, size});
    REQUIRE_MESSAGE(image.ok(), image.error().code << ": " << image.error().message);
    return std::move(image.value());
}

std::unique_ptr<Gpu::Device> MakeDevice() {
    Gpu::DeviceOptions options;
    options.validation = true;
    auto device = Gpu::Device::Create(options);
    REQUIRE_MESSAGE(device.ok(), device.error().code << ": " << device.error().message);
    RequireRenderValidationLayer(*device.value());
    return std::move(device.value());
}

}

TEST_CASE("Grid overlay changes pixels only below the horizon of an empty scene") {
    auto device = MakeDevice();
    const Domain::Scene empty;
    // A level camera one unit above the plane puts the horizon at the middle of the image.
    Render::View view;
    view.view = glm::lookAt(glm::vec3(0.0f, 1.0f, 5.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    view.proj = Render::Projection::Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    constexpr uint32_t size = 256;

    Render::Overlays off;
    Render::Overlays on;
    on.grid = true;
    const Domain::Texture a = RenderOrFail(*device, empty, view, off, size);
    const Domain::Texture b = RenderOrFail(*device, empty, view, on, size);

    const auto all = TestKit::CompareRgba(size, size, a.pixels, b.pixels);
    CHECK_MESSAGE(all.differingPct > 0.0, TestKit::FormatMetrics(all));

    constexpr uint32_t topRows = size / 10;
    const size_t topBytes = static_cast<size_t>(topRows) * size * 4;
    const auto top = TestKit::CompareRgba(size, topRows, std::span<const std::byte>(a.pixels).first(topBytes),
                                          std::span<const std::byte>(b.pixels).first(topBytes));
    CHECK_MESSAGE(top.maxChannelDelta == 0, TestKit::FormatMetrics(top));
    CHECK_MESSAGE(device->ValidationMessageCount() == 0, device->LastValidationMessage());
}

TEST_CASE("Skeleton overlay draws bones over skinned-cube") {
    auto device = MakeDevice();
    const auto fixture = TestKit::BuildSkinnedCube();
    const Render::View view = ViewOf(fixture);
    constexpr uint32_t size = 256;

    Render::Overlays off;
    Render::Overlays on;
    on.skeleton = true;
    const Domain::Texture a = RenderOrFail(*device, fixture.scene, view, off, size);
    const Domain::Texture b = RenderOrFail(*device, fixture.scene, view, on, size);

    const auto result = TestKit::CompareRgba(size, size, a.pixels, b.pixels);
    CHECK_MESSAGE(result.differingPct > 0.1, TestKit::FormatMetrics(result));
    CHECK_MESSAGE(device->ValidationMessageCount() == 0, device->LastValidationMessage());
}

TEST_CASE("Skeleton overlay of joint-chain-200 fits the default transient ring") {
    auto device = MakeDevice();
    const auto fixture = TestKit::BuildJointChain200();
    Render::Overlays overlays;
    overlays.skeleton = true;
    overlays.grid = true;
    auto image = Render::RenderToImage(*device, fixture.scene, ViewOf(fixture), overlays, {256, 256});
    REQUIRE_MESSAGE(image.ok(), image.error().code << ": " << image.error().message);
    CHECK(image.value().pixels.size() == 256u * 256u * 4u);
    CHECK_MESSAGE(device->ValidationMessageCount() == 0, device->LastValidationMessage());
}
