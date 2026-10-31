#pragma once

#include <doctest/doctest.h>

#include <Onyx/Gpu/Device.h>
#include <Onyx/Gpu/FrameRing.h>
#include <Onyx/Gpu/RenderTarget.h>
#include <Onyx/Gpu/Upload.h>
#include <Onyx/Render/Projection.h>
#include <Onyx/Render/SceneRenderer.h>
#include <Onyx/Render/View.h>
#include <Onyx/TestKit/Png.h>
#include <Onyx/TestKit/SceneFixtures.h>

#include <glm/gtc/constants.hpp>

#include <memory>

namespace Onyx {

struct RenderFixture {
    std::unique_ptr<Gpu::Device> device;
    std::unique_ptr<Gpu::UploadContext> upload;
    std::unique_ptr<Gpu::FrameRing> frames;
    std::unique_ptr<Render::SceneRenderer> renderer;
};

inline RenderFixture MakeRenderFixture(const Render::RendererOptions& options = {}) {
    RenderFixture fx;
    Gpu::DeviceOptions deviceOptions;
    deviceOptions.validation = true;
    auto device = Gpu::Device::Create(deviceOptions);
    REQUIRE_MESSAGE(device.ok(), device.error().code << ": " << device.error().message);
    fx.device = std::move(device.value());

    auto upload = Gpu::UploadContext::Create(*fx.device);
    REQUIRE_MESSAGE(upload.ok(), upload.error().code << ": " << upload.error().message);
    fx.upload = std::move(upload.value());

    auto frames = Gpu::FrameRing::Create(*fx.device);
    REQUIRE_MESSAGE(frames.ok(), frames.error().code << ": " << frames.error().message);
    fx.frames = std::move(frames.value());

    auto renderer = Render::SceneRenderer::Create(*fx.device, options);
    REQUIRE_MESSAGE(renderer.ok(), renderer.error().code << ": " << renderer.error().message);
    fx.renderer = std::move(renderer.value());
    return fx;
}

// The camera of a corpus fixture: its look-at matrix and the perspective of its field of view.
inline Render::View ViewOf(const TestKit::SceneFixture& fixture) {
    Render::View view;
    view.view = fixture.view;
    view.proj = Render::Projection::Perspective(glm::radians(fixture.fovYDegrees),
                                                static_cast<float>(fixture.width) / static_cast<float>(fixture.height),
                                                fixture.nearPlane, fixture.farPlane);
    view.mode = fixture.textured ? Render::ShadingMode::Textured : Render::ShadingMode::Solid;
    return view;
}

// Renders one frame into a fresh target and reads it back, top row first.
inline Result<TestKit::Image8> RenderOnce(RenderFixture& fx, const Render::GpuScene& scene, const Render::View& view,
                                          const Render::Overlays& overlays, uint32_t width, uint32_t height) {
    const Render::RendererOptions& options = fx.renderer->Options();
    Gpu::RenderTargetDesc desc;
    desc.extent = {width, height};
    desc.colorFormat = options.colorFormat;
    desc.depthFormat = options.depthFormat;
    desc.samples = options.samples;
    auto target = Gpu::RenderTarget::Create(*fx.device, desc);
    if (!target.ok()) {
        return Result<TestKit::Image8>::Fail(target.error());
    }
    auto frame = fx.frames->Begin();
    if (!frame.ok()) {
        return Result<TestKit::Image8>::Fail(frame.error());
    }
    auto drawn = fx.renderer->Draw(*frame.value(), *target.value(), scene, view, overlays);
    auto ended = fx.frames->End(*frame.value());
    if (!drawn.ok()) {
        return Result<TestKit::Image8>::Fail(drawn.error());
    }
    if (!ended.ok()) {
        return Result<TestKit::Image8>::Fail(ended.error());
    }
    auto pixels = Gpu::ReadbackColor(*fx.device, *target.value(), ended.value());
    if (!pixels.ok()) {
        return Result<TestKit::Image8>::Fail(pixels.error());
    }
    TestKit::Image8 image;
    image.width = width;
    image.height = height;
    image.rgba = std::move(pixels.value());
    return Result<TestKit::Image8>::Ok(std::move(image));
}

}
