#include <doctest/doctest.h>

#include <Onyx/Render/RenderToImage.h>
#include <Onyx/TestKit/ImageCompare.h>

#include "Support/RenderFixture.h"

using namespace Onyx;

TEST_CASE("RenderToImage renders blend-stack identically to the frame-ring path") {
    auto fx = MakeRenderFixture();
    const auto fixture = TestKit::BuildBlendStack();
    const Render::View view = ViewOf(fixture);
    constexpr uint32_t size = 128;

    auto loaded = fx.renderer->Load(fixture.scene, *fx.upload);
    REQUIRE(loaded.ok());
    auto ring = RenderOnce(fx, loaded.value(), view, {}, size, size);
    REQUIRE_MESSAGE(ring.ok(), ring.error().code << ": " << ring.error().message);

    auto direct = Render::RenderToImage(*fx.device, fixture.scene, view, {}, {size, size});
    REQUIRE_MESSAGE(direct.ok(), direct.error().code << ": " << direct.error().message);
    CHECK(direct.value().name == "render");
    CHECK(direct.value().width == size);
    CHECK(direct.value().format == Domain::PixelFormat::Rgba8);
    const auto result = TestKit::CompareRgba(size, size, ring.value().rgba, direct.value().pixels);
    CHECK_MESSAGE(result.maxChannelDelta == 0, TestKit::FormatMetrics(result));

    // The reusing overload draws the same image and serves a second scene on the same renderer.
    auto reused = Render::RenderToImage(*fx.renderer, *fx.upload, *fx.frames, fixture.scene, view, {}, {size, size});
    REQUIRE_MESSAGE(reused.ok(), reused.error().code << ": " << reused.error().message);
    CHECK(reused.value().pixels == direct.value().pixels);
    const auto cube = TestKit::BuildSkinnedCube();
    auto second =
        Render::RenderToImage(*fx.renderer, *fx.upload, *fx.frames, cube.scene, ViewOf(cube), {}, {size, size});
    CHECK(second.ok());
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("RenderToImage reports a failing scene instead of crashing") {
    auto fx = MakeRenderFixture();
    const auto fixture = TestKit::BuildBlendStack();
    auto image = Render::RenderToImage(*fx.device, fixture.scene, ViewOf(fixture), {}, {0, 0});
    REQUIRE_FALSE(image.ok());
    CHECK(image.error().code == "gpu.extent");
    auto reused =
        Render::RenderToImage(*fx.renderer, *fx.upload, *fx.frames, fixture.scene, ViewOf(fixture), {}, {0, 64});
    REQUIRE_FALSE(reused.ok());
    CHECK(reused.error().code == "gpu.extent");
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}
