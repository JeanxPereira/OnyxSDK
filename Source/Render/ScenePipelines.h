#pragma once

#include <Onyx/Core/Result.h>
#include <Onyx/Gpu/Device.h>
#include <Onyx/Gpu/Pipeline.h>
#include <Onyx/Render/SceneRenderer.h>

namespace Onyx::Render {

// Descriptor sets of the scene pipelines: 0 frame (dynamic UBO + dynamic palette SSBO), 1 material
// (UBO + seven samplers), 2 part (joint map SSBO). The background pipeline has set 0 only.
struct ScenePipelines {
    Gpu::Pipeline opaque;
    Gpu::Pipeline alpha;
    Gpu::Pipeline additive;
    Gpu::Pipeline sky;
    Gpu::Pipeline background;
};

Result<ScenePipelines> BuildScenePipelines(Gpu::Device& device, const RendererOptions& options);

}
