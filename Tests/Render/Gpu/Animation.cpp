#include <doctest/doctest.h>

#include <Onyx/Render/AnimationPlayer.h>
#include <Onyx/Render/RenderToImage.h>
#include <Onyx/TestKit/ImageCompare.h>
#include <Onyx/TestKit/Tolerances.h>

#include <algorithm>
#include <filesystem>
#include <span>
#include <vector>

#include "Support/RenderFixture.h"

using namespace Onyx;

namespace {

TestKit::CompareTolerance Hardware() {
    auto tolerance = TestKit::ReadTolerances(std::filesystem::path(ONYX_GOLDEN_DIR) / "tolerances.toml", "hardware");
    REQUIRE_MESSAGE(tolerance.ok(), tolerance.error().message);
    return tolerance.value();
}

}

TEST_CASE("Animation gate: rest, set, mid-clip, stopped, resumed") {
    constexpr uint32_t size = 512;
    const auto tolerance = Hardware();
    const auto fixture = TestKit::BuildAnimatedChain();
    Gpu::DeviceOptions options;
    options.validation = true;
    auto device = Gpu::Device::Create(options);
    REQUIRE(device.ok());
    Gpu::Device& gpu = *device.value();

    Render::View view = ViewOf(fixture);
    auto render = [&](const Render::View& v) {
        auto image = Render::RenderToImage(gpu, fixture.scene, v, {}, {size, size});
        REQUIRE_MESSAGE(image.ok(), image.error().code << ": " << image.error().message);
        return std::move(image.value());
    };
    auto compare = [&](const Domain::Texture& a, const Domain::Texture& b) {
        return TestKit::CompareRgba(size, size, a.pixels, b.pixels);
    };

    const Domain::Texture rest = render(view);

    view.pose = Render::ClipTime{0, 0.0f};
    const auto atZero = compare(rest, render(view));
    CHECK_MESSAGE(TestKit::WithinTolerance(atZero, tolerance), TestKit::FormatMetrics(atZero));

    view.pose = Render::ClipTime{0, 0.5f};
    const auto mid = compare(rest, render(view));
    CHECK_MESSAGE(!TestKit::WithinTolerance(mid, tolerance), TestKit::FormatMetrics(mid));

    view.pose = std::nullopt;
    const auto cleared = compare(rest, render(view));
    CHECK_MESSAGE(TestKit::WithinTolerance(cleared, tolerance), TestKit::FormatMetrics(cleared));
    CHECK_MESSAGE(gpu.ValidationMessageCount() == 0, gpu.LastValidationMessage());
}

TEST_CASE("Animation gate: AnimationPlayer drives a GpuScene through play, stop and resume") {
    constexpr uint32_t size = 512;
    const auto tolerance = Hardware();
    const auto fixture = TestKit::BuildAnimatedChain();
    auto fx = MakeRenderFixture();
    const Render::View view = ViewOf(fixture);

    auto loaded = fx.renderer->Load(fixture.scene, *fx.upload);
    REQUIRE(loaded.ok());
    Render::GpuScene& scene = loaded.value();
    const std::vector<glm::mat4> restPalette(scene.Palette().begin(), scene.Palette().end());
    const std::vector<glm::mat4> restWorlds(scene.WorldTransforms().begin(), scene.WorldTransforms().end());
    REQUIRE_FALSE(restPalette.empty());

    auto draw = [&]() {
        auto image = RenderOnce(fx, scene, view, {}, size, size);
        REQUIRE_MESSAGE(image.ok(), image.error().code << ": " << image.error().message);
        return std::move(image.value());
    };
    auto compare = [&](const TestKit::Image8& a, const TestKit::Image8& b) {
        return TestKit::CompareRgba(size, size, a.rgba, b.rgba);
    };
    auto apply = [&](const Render::AnimationPlayer& player) {
        scene.SetPalette(player.Palette());
        scene.SetWorldTransforms(player.WorldTransforms());
    };
    auto equal = [](std::span<const glm::mat4> a, const std::vector<glm::mat4>& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
    };

    const TestKit::Image8 rest = draw();

    Render::AnimationPlayer player;
    player.Set(scene.Skeleton(), scene.Animations(), 0);
    apply(player);
    const auto set = compare(rest, draw());
    CHECK_MESSAGE(TestKit::WithinTolerance(set, tolerance), TestKit::FormatMetrics(set));

    player.SetPlaying(true);
    CHECK(player.Advance(0.5f));
    apply(player);
    const auto mid = compare(rest, draw());
    CHECK_MESSAGE(!TestKit::WithinTolerance(mid, tolerance), TestKit::FormatMetrics(mid));

    player.Stop();
    apply(player);
    CHECK(equal(scene.Palette(), restPalette));
    CHECK(equal(scene.WorldTransforms(), restWorlds));
    const auto stopped = compare(rest, draw());
    CHECK_MESSAGE(TestKit::WithinTolerance(stopped, tolerance), TestKit::FormatMetrics(stopped));

    player.Advance(1.0f / 60.0f);
    apply(player);
    CHECK(equal(scene.Palette(), restPalette));
    const auto resumed = compare(rest, draw());
    CHECK_MESSAGE(TestKit::WithinTolerance(resumed, tolerance), TestKit::FormatMetrics(resumed));
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}
