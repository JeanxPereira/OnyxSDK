#include "Onyx/Render/RenderToImage.h"

#include <Onyx/Gpu/RenderTarget.h>
#include <Onyx/Render/AnimationPlayer.h>

#include <utility>

namespace Onyx::Render {

Result<Domain::Texture> RenderToImage(SceneRenderer& renderer, Gpu::UploadContext& upload, Gpu::FrameRing& frames,
                                      const Domain::Scene& scene, const View& view, const Overlays& overlays,
                                      Extent extent) {
    Gpu::Device& device = renderer.GetDevice();
    const RendererOptions& options = renderer.Options();
    Gpu::RenderTargetDesc desc;
    desc.extent = {extent.width, extent.height};
    desc.colorFormat = options.colorFormat;
    desc.depthFormat = options.depthFormat;
    desc.samples = options.samples;
    auto target = Gpu::RenderTarget::Create(device, desc);
    if (!target.ok()) {
        return Result<Domain::Texture>::Fail(target.error());
    }

    auto loaded = renderer.Load(scene, upload);
    if (!loaded.ok()) {
        return Result<Domain::Texture>::Fail(loaded.error());
    }
    GpuScene& gpuScene = loaded.value();

    if (view.pose && gpuScene.Skeleton() && gpuScene.Animations()) {
        AnimationPlayer player;
        player.Set(gpuScene.Skeleton(), gpuScene.Animations(), view.pose->clip);
        player.SetTime(view.pose->time);
        gpuScene.SetPalette(player.Palette());
        gpuScene.SetWorldTransforms(player.WorldTransforms());
    }

    auto frame = frames.Begin();
    if (!frame.ok()) {
        return Result<Domain::Texture>::Fail(frame.error());
    }
    auto drawn = renderer.Draw(*frame.value(), *target.value(), gpuScene, view, overlays);
    // The frame is open until End, whether or not the draw recorded anything.
    auto ended = frames.End(*frame.value());
    if (!drawn.ok()) {
        return Result<Domain::Texture>::Fail(drawn.error());
    }
    if (!ended.ok()) {
        return Result<Domain::Texture>::Fail(ended.error());
    }
    auto pixels = Gpu::ReadbackColor(device, *target.value(), ended.value());
    if (!pixels.ok()) {
        return Result<Domain::Texture>::Fail(pixels.error());
    }

    Domain::Texture image;
    image.name = "render";
    image.width = extent.width;
    image.height = extent.height;
    image.format = Domain::PixelFormat::Rgba8;
    image.pixels = std::move(pixels.value());
    return Result<Domain::Texture>::Ok(std::move(image));
}

Result<Domain::Texture> RenderToImage(Gpu::Device& device, const Domain::Scene& scene, const View& view,
                                      const Overlays& overlays, Extent extent, const RendererOptions& options) {
    auto upload = Gpu::UploadContext::Create(device);
    if (!upload.ok()) {
        return Result<Domain::Texture>::Fail(upload.error());
    }
    auto renderer = SceneRenderer::Create(device, options);
    if (!renderer.ok()) {
        return Result<Domain::Texture>::Fail(renderer.error());
    }
    auto frames = Gpu::FrameRing::Create(device, {.framesInFlight = 1});
    if (!frames.ok()) {
        return Result<Domain::Texture>::Fail(frames.error());
    }
    return RenderToImage(*renderer.value(), *upload.value(), *frames.value(), scene, view, overlays, extent);
}

}
