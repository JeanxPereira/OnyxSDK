#pragma once

#include <Onyx/Core/Domain/Scene.h>
#include <Onyx/Core/Domain/Texture.h>
#include <Onyx/Core/Result.h>
#include <Onyx/Gpu/Device.h>
#include <Onyx/Gpu/FrameRing.h>
#include <Onyx/Gpu/Upload.h>
#include <Onyx/Render/SceneRenderer.h>
#include <Onyx/Render/View.h>

#include <cstdint>

namespace Onyx::Render {

struct Extent {
    uint32_t width = 0;
    uint32_t height = 0;
};

// Renders `scene` once into a fresh target and reads it back as an RGBA8 texture named "render", top
// row first. When view.pose is set the scene is posed at that clip and time before the draw. Blocks
// until the GPU has finished. Error codes: gpu.extent (a zero dimension), plus every code of
// SceneRenderer::Create, Load and Draw and of the layer below.
//
// This overload creates its own UploadContext, SceneRenderer and one-frame FrameRing.
Result<Domain::Texture> RenderToImage(Gpu::Device& device, const Domain::Scene& scene, const View& view,
                                      const Overlays& overlays, Extent extent, const RendererOptions& options = {});

// The same render through objects the caller owns, so several scenes share one renderer. `frames`
// must be the ring the renderer is paired with, and the target takes the renderer's options.
Result<Domain::Texture> RenderToImage(SceneRenderer& renderer, Gpu::UploadContext& upload, Gpu::FrameRing& frames,
                                      const Domain::Scene& scene, const View& view, const Overlays& overlays,
                                      Extent extent);

}
