#include <doctest/doctest.h>

#include <Onyx/Core/Services/Diagnostics.h>
#include <Onyx/Gpu/RenderTarget.h>
#include <Onyx/TestKit/ImageCompare.h>
#include <Onyx/TestKit/Tolerances.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "Support/RenderFixture.h"

using namespace Onyx;

namespace {

bool HasCode(const Services::DiagSink& sink, const std::string& code) {
    for (const Services::Diag& diag : sink.Snapshot()) {
        if (diag.code == code) {
            return true;
        }
    }
    return false;
}

size_t CountDifferingFrom(const TestKit::Image8& image, std::array<int, 3> rgb, int slack) {
    size_t count = 0;
    for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        bool same = true;
        for (size_t c = 0; c < 3; ++c) {
            same = same && std::abs(static_cast<int>(image.rgba[i + c]) - rgb[c]) <= slack;
        }
        count += same ? 0 : 1;
    }
    return count;
}

std::array<int, 3> ClearBytes(const Render::View& view) {
    return {static_cast<int>(std::lround(view.clear.r * 255.0f)), static_cast<int>(std::lround(view.clear.g * 255.0f)),
            static_cast<int>(std::lround(view.clear.b * 255.0f))};
}

TestKit::CompareTolerance Hardware() {
    auto tolerance = TestKit::ReadTolerances(std::filesystem::path(ONYX_GOLDEN_DIR) / "tolerances.toml", "hardware");
    REQUIRE_MESSAGE(tolerance.ok(), tolerance.error().message);
    return tolerance.value();
}

Render::GpuScene LoadOrFail(RenderFixture& fx, const Domain::Scene& scene, Services::DiagSink* diags = nullptr) {
    auto loaded = fx.renderer->Load(scene, *fx.upload, diags);
    REQUIRE_MESSAGE(loaded.ok(), loaded.error().code << ": " << loaded.error().message);
    return std::move(loaded.value());
}

TestKit::Image8 RenderOrFail(RenderFixture& fx, const Render::GpuScene& scene, const Render::View& view,
                             uint32_t size = 128) {
    auto image = RenderOnce(fx, scene, view, {}, size, size);
    REQUIRE_MESSAGE(image.ok(), image.error().code << ": " << image.error().message);
    return std::move(image.value());
}

}

TEST_CASE("Load flushes exactly once and Draw submits nothing") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildBlendStack();
    auto before = fx.device->SubmitCount();
    auto scene = fx.renderer->Load(fixture.scene, *fx.upload);
    REQUIRE(scene.ok());
    CHECK(fx.device->SubmitCount() == before + 1);
    auto frame = fx.frames->Begin();
    REQUIRE(frame.ok());
    auto rt = Gpu::RenderTarget::Create(*fx.device, {.extent = {64, 64}});
    REQUIRE(rt.ok());
    auto mid = fx.device->SubmitCount();
    CHECK(fx.renderer->Draw(*frame.value(), *rt.value(), scene.value(), ViewOf(fixture), {}).ok());
    CHECK(fx.device->SubmitCount() == mid);
    CHECK(fx.frames->End(*frame.value()).ok());
    CHECK(fx.device->SubmitCount() == mid + 1);
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("Blend-stack renders non-uniform output and is byte-identical across two renders") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildBlendStack();
    auto scene = LoadOrFail(fx, fixture.scene);
    const Render::View view = ViewOf(fixture);
    const TestKit::Image8 a = RenderOrFail(fx, scene, view);
    const TestKit::Image8 b = RenderOrFail(fx, scene, view);
    CHECK(CountDifferingFrom(a, ClearBytes(view), 1) > 500);
    REQUIRE(a.rgba.size() == b.rgba.size());
    CHECK(a.rgba == b.rgba);
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("Sphere-grid textured differs from sphere-grid solid") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildSphereGrid();
    auto scene = LoadOrFail(fx, fixture.scene);
    Render::View solid = ViewOf(fixture);
    solid.mode = Render::ShadingMode::Solid;
    Render::View textured = solid;
    textured.mode = Render::ShadingMode::Textured;
    const TestKit::Image8 a = RenderOrFail(fx, scene, solid, 256);
    const TestKit::Image8 b = RenderOrFail(fx, scene, textured, 256);
    const TestKit::ImageCompareResult result = TestKit::CompareRgba(256, 256, a.rgba, b.rgba);
    CHECK_MESSAGE(result.differingPct > 1.0, TestKit::FormatMetrics(result));
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("Materials are deduplicated") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildBlendStack();
    Domain::Scene scene = fixture.scene;
    const Domain::MeshPart quad = scene.parts[0];
    const Domain::Material a = scene.materials[0];
    const Domain::Material b = scene.materials[2];
    scene.materials = {a, b, a, b};
    scene.parts.clear();
    for (uint32_t material : {0u, 1u, 2u, 3u, 0u, 1u}) {
        Domain::MeshPart part = quad;
        part.materialIndex = material;
        scene.parts.push_back(std::move(part));
    }
    auto gpu = LoadOrFail(fx, scene);
    CHECK(gpu.Draws().size() == 6);
    CHECK(gpu.MaterialCount() == 2);
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("Skinned-cube with an identity palette equals skinned-cube without skinning data") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildSkinnedCube();
    const Render::View view = ViewOf(fixture);

    auto skinned = LoadOrFail(fx, fixture.scene);
    REQUIRE(skinned.Draws().size() == 1);
    CHECK(skinned.Draws()[0].skinned);
    CHECK(skinned.Draws()[0].jointCount == 3);
    CHECK(skinned.Palette().size() == 3);
    const TestKit::Image8 rest = RenderOrFail(fx, skinned, view);
    const std::vector<glm::mat4> identity(3, glm::mat4(1.0f));
    skinned.SetPalette(identity);
    const TestKit::Image8 bound = RenderOrFail(fx, skinned, view);

    Domain::Scene flatScene = fixture.scene;
    flatScene.parts[0].joints.clear();
    flatScene.parts[0].weights.clear();
    flatScene.parts[0].jointMap.clear();
    auto flat = LoadOrFail(fx, flatScene);
    CHECK_FALSE(flat.Draws()[0].skinned);
    const TestKit::Image8 reference = RenderOrFail(fx, flat, view);

    const TestKit::ImageCompareResult same = TestKit::CompareRgba(128, 128, bound.rgba, reference.rgba);
    CHECK_MESSAGE(TestKit::WithinTolerance(same, Hardware()), TestKit::FormatMetrics(same));
    const TestKit::ImageCompareResult bent = TestKit::CompareRgba(128, 128, rest.rgba, reference.rgba);
    CHECK_MESSAGE(bent.differingPct > 0.5, TestKit::FormatMetrics(bent));
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("A part with an out-of-range joint index is reported and skipped") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildSkinnedCube();
    Domain::MeshPart good = fixture.scene.parts[0];
    good.name = "good";
    Domain::MeshPart bad = fixture.scene.parts[0];
    bad.name = "bad";
    bad.joints[0].x = 200;
    fixture.scene.parts = {bad, good};

    Services::DiagSink diags;
    auto gpu = LoadOrFail(fx, fixture.scene, &diags);
    CHECK(HasCode(diags, "render.skinning.jointIndex"));
    REQUIRE(gpu.Draws().size() == 1);
    CHECK(gpu.Draws()[0].name == "good");
    const TestKit::Image8 image = RenderOrFail(fx, gpu, ViewOf(fixture));
    CHECK(CountDifferingFrom(image, ClearBytes(ViewOf(fixture)), 1) > 100);
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("Of two parts sharing a name only the offending one is skipped") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildSkinnedCube();
    Domain::MeshPart good = fixture.scene.parts[0];
    Domain::MeshPart bad = fixture.scene.parts[0];
    bad.joints[0].x = 200;
    fixture.scene.parts = {good, bad, good};

    Services::DiagSink diags;
    auto gpu = LoadOrFail(fx, fixture.scene, &diags);
    CHECK(HasCode(diags, "render.skinning.jointIndex"));
    CHECK(gpu.Draws().size() == 2);
}

TEST_CASE("A part with an index outside its vertices is reported and skipped") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildBlendStack();
    fixture.scene.parts[1].indices[2] = 99;
    Services::DiagSink diags;
    auto gpu = LoadOrFail(fx, fixture.scene, &diags);
    CHECK(HasCode(diags, "render.part.index"));
    CHECK(gpu.Draws().size() == 3);
    RenderOrFail(fx, gpu, ViewOf(fixture));
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("A short texture is reported and the default is bound") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildBlendStack();
    fixture.scene.textures[0].pixels.resize(10);
    Services::DiagSink diags;
    auto gpu = LoadOrFail(fx, fixture.scene, &diags);
    CHECK(HasCode(diags, "render.texture.size"));
    CHECK(gpu.Draws()[0].texturesBound == 0);
    const TestKit::Image8 image = RenderOrFail(fx, gpu, ViewOf(fixture));
    CHECK(CountDifferingFrom(image, ClearBytes(ViewOf(fixture)), 1) > 500);
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("A material texture index past the pool is reported") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildBlendStack();
    fixture.scene.materials[0].textures[Domain::TextureRole::Diffuse] = 99;
    Services::DiagSink diags;
    auto gpu = LoadOrFail(fx, fixture.scene, &diags);
    CHECK(HasCode(diags, "render.material.index"));
    RenderOrFail(fx, gpu, ViewOf(fixture));
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("A part with a material index past the pool is reported and drawn with the default material") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildBlendStack();
    fixture.scene.parts[0].materialIndex = 50;
    Services::DiagSink diags;
    auto gpu = LoadOrFail(fx, fixture.scene, &diags);
    CHECK(HasCode(diags, "render.material.index"));
    CHECK(gpu.Draws().size() == 4);
    RenderOrFail(fx, gpu, ViewOf(fixture));
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("Hidden parts are not uploaded") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildBlendStack();
    fixture.scene.parts[1].flags = Domain::PartFlags::Hidden;
    auto gpu = LoadOrFail(fx, fixture.scene);
    CHECK(gpu.Draws().size() == fixture.scene.parts.size() - 1);
    for (const Render::DrawInfo& draw : gpu.Draws()) {
        CHECK(draw.name != fixture.scene.parts[1].name);
    }
}

TEST_CASE("Draw into a resized target under validation stays clean") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildBlendStack();
    auto gpu = LoadOrFail(fx, fixture.scene);
    const Render::View view = ViewOf(fixture);
    auto rt = Gpu::RenderTarget::Create(*fx.device, {.extent = {64, 64}});
    REQUIRE(rt.ok());

    auto first = fx.frames->Begin();
    REQUIRE(first.ok());
    CHECK(fx.renderer->Draw(*first.value(), *rt.value(), gpu, view, {}).ok());
    CHECK(fx.frames->End(*first.value()).ok());

    REQUIRE(rt.value()->Resize({96, 96}).ok());
    auto second = fx.frames->Begin();
    REQUIRE(second.ok());
    CHECK(fx.renderer->Draw(*second.value(), *rt.value(), gpu, view, {}).ok());
    auto done = fx.frames->End(*second.value());
    REQUIRE(done.ok());
    CHECK(fx.device->GraphicsQueue().Wait(done.value()).ok());
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("The background gradient fills the cleared pixels") {
    auto fx = MakeRenderFixture();
    Domain::Scene empty;
    auto gpu = LoadOrFail(fx, empty);
    CHECK(gpu.Draws().empty());
    Render::View view;
    view.background = Render::Gradient{{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    const TestKit::Image8 image = RenderOrFail(fx, gpu, view, 64);
    const auto at = [&](uint32_t y, size_t channel) { return static_cast<int>(image.rgba[(y * 64 + 32) * 4 + channel]); };
    CHECK(at(2, 0) > 200);
    CHECK(at(2, 2) < 60);
    CHECK(at(61, 0) < 60);
    CHECK(at(61, 2) > 200);
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("A sky part draws with the rotation-only view and validation stays clean") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildBlendStack();
    fixture.scene.parts[0].flags = Domain::PartFlags::Sky;
    auto gpu = LoadOrFail(fx, fixture.scene);
    REQUIRE(gpu.Draws().size() == 4);
    CHECK(gpu.Draws()[0].sky);
    RenderOrFail(fx, gpu, ViewOf(fixture));
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("A palette of the wrong size is ignored and reported at Draw") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildSkinnedCube();
    auto gpu = LoadOrFail(fx, fixture.scene);
    const std::vector<glm::mat4> wrong(5, glm::mat4(1.0f));
    gpu.SetPalette(wrong);
    CHECK(gpu.Palette().size() == 3);

    auto target = Gpu::RenderTarget::Create(*fx.device, {.extent = {32, 32}});
    REQUIRE(target.ok());
    auto frame = fx.frames->Begin();
    REQUIRE(frame.ok());
    Services::DiagSink diags;
    CHECK(fx.renderer->Draw(*frame.value(), *target.value(), gpu, ViewOf(fixture), {}, &diags).ok());
    CHECK(fx.frames->End(*frame.value()).ok());
    CHECK(HasCode(diags, "render.palette.size"));
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("Draw rejects a target whose formats differ from the renderer's options") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildBlendStack();
    auto gpu = LoadOrFail(fx, fixture.scene);
    auto target = Gpu::RenderTarget::Create(*fx.device, {.extent = {32, 32}, .samples = VK_SAMPLE_COUNT_1_BIT});
    REQUIRE(target.ok());
    auto frame = fx.frames->Begin();
    REQUIRE(frame.ok());
    auto drawn = fx.renderer->Draw(*frame.value(), *target.value(), gpu, ViewOf(fixture), {});
    REQUIRE_FALSE(drawn.ok());
    CHECK(drawn.error().code == "render.target.format");
    CHECK(fx.frames->End(*frame.value()).ok());
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}

TEST_CASE("The 200-joint chain loads and renders under validation") {
    auto fx = MakeRenderFixture();
    auto fixture = TestKit::BuildJointChain200();
    Services::DiagSink diags;
    auto gpu = LoadOrFail(fx, fixture.scene, &diags);
    CHECK(diags.Count(Services::Severity::Error) == 0);
    CHECK(gpu.Draws().size() == fixture.scene.parts.size());
    const TestKit::Image8 image = RenderOrFail(fx, gpu, ViewOf(fixture));
    CHECK(CountDifferingFrom(image, ClearBytes(ViewOf(fixture)), 1) > 200);
    CHECK_MESSAGE(fx.device->ValidationMessageCount() == 0, fx.device->LastValidationMessage());
}
