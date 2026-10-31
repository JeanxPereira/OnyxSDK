#include "ScenePipelines.h"

#include <Spirv/BackgroundFrag.h>
#include <Spirv/BackgroundVert.h>
#include <Spirv/GridFrag.h>
#include <Spirv/GridVert.h>
#include <Spirv/OverlayFrag.h>
#include <Spirv/OverlayVert.h>
#include <Spirv/SceneFrag.h>
#include <Spirv/SceneVert.h>

#include <array>
#include <span>

#include "ShaderTypes.h"

namespace Onyx::Render {

namespace {

using Gpu::BlendState;

constexpr VkShaderStageFlags kVertFrag = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

VkDescriptorSetLayoutBinding Binding(uint32_t binding, VkDescriptorType type, uint32_t count, VkShaderStageFlags stages) {
    VkDescriptorSetLayoutBinding b{};
    b.binding = binding;
    b.descriptorType = type;
    b.descriptorCount = count;
    b.stageFlags = stages;
    return b;
}

std::span<const uint32_t> SceneVert() { return {Spirv::kSceneVert, Spirv::kSceneVertWords}; }
std::span<const uint32_t> SceneFrag() { return {Spirv::kSceneFrag, Spirv::kSceneFragWords}; }
std::span<const uint32_t> BackgroundVert() { return {Spirv::kBackgroundVert, Spirv::kBackgroundVertWords}; }
std::span<const uint32_t> BackgroundFrag() { return {Spirv::kBackgroundFrag, Spirv::kBackgroundFragWords}; }

std::span<const uint32_t> GridVert() { return {Spirv::kGridVert, Spirv::kGridVertWords}; }
std::span<const uint32_t> GridFrag() { return {Spirv::kGridFrag, Spirv::kGridFragWords}; }
std::span<const uint32_t> OverlayVert() { return {Spirv::kOverlayVert, Spirv::kOverlayVertWords}; }
std::span<const uint32_t> OverlayFrag() { return {Spirv::kOverlayFrag, Spirv::kOverlayFragWords}; }

BlendState Alpha() {
    BlendState blend;
    blend.enabled = true;
    blend.srcColor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.srcAlpha = blend.srcColor;
    blend.dstAlpha = blend.dstColor;
    return blend;
}

BlendState Additive() {
    BlendState blend;
    blend.enabled = true;
    blend.srcColor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColor = VK_BLEND_FACTOR_ONE;
    blend.srcAlpha = blend.srcColor;
    blend.dstAlpha = blend.dstColor;
    return blend;
}

Result<Gpu::Pipeline> BuildScene(Gpu::Device& device, const RendererOptions& options, const BlendState& blend,
                                 bool depthWrite, VkCompareOp compare) {
    constexpr uint32_t stride = sizeof(Shader::SceneVertex);
    const std::array bindings{Gpu::VertexBinding{0, stride, VK_VERTEX_INPUT_RATE_VERTEX}};
    const std::array attributes{
        Gpu::VertexAttribute{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Shader::SceneVertex, position)},
        Gpu::VertexAttribute{1, 0, VK_FORMAT_R16G16B16A16_SNORM, offsetof(Shader::SceneVertex, normal)},
        Gpu::VertexAttribute{2, 0, VK_FORMAT_R16G16B16A16_SNORM, offsetof(Shader::SceneVertex, tangent)},
        Gpu::VertexAttribute{3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Shader::SceneVertex, uv0)},
        Gpu::VertexAttribute{4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Shader::SceneVertex, uv1)},
        Gpu::VertexAttribute{5, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Shader::SceneVertex, color)},
        Gpu::VertexAttribute{6, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(Shader::SceneVertex, joints)},
        Gpu::VertexAttribute{7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Shader::SceneVertex, weights)},
    };
    const std::array frameSet{
        Binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, kVertFrag),
        Binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT),
    };
    const std::array materialSet{
        Binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, kVertFrag),
        Binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, Shader::kRoleCount, VK_SHADER_STAGE_FRAGMENT_BIT),
    };
    const std::array partSet{Binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)};

    Gpu::PipelineBuilder builder(device);
    builder.Vertex(SceneVert())
        .Fragment(SceneFrag())
        .VertexInput(bindings, attributes)
        .Topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .SetLayout(frameSet)
        .SetLayout(materialSet)
        .SetLayout(partSet)
        .PushConstants(VK_SHADER_STAGE_VERTEX_BIT, sizeof(Shader::ScenePush))
        .Color(options.colorFormat, blend)
        .Samples(options.samples)
        .Cull(VK_CULL_MODE_NONE);
    if (options.depthFormat != VK_FORMAT_UNDEFINED) {
        builder.Depth(options.depthFormat, true, depthWrite, compare);
    }
    return builder.Build();
}

Result<Gpu::Pipeline> BuildBackground(Gpu::Device& device, const RendererOptions& options) {
    const std::array set{Binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT)};
    Gpu::PipelineBuilder builder(device);
    builder.Vertex(BackgroundVert())
        .Fragment(BackgroundFrag())
        .Topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .SetLayout(set)
        .Color(options.colorFormat)
        .Samples(options.samples)
        .Cull(VK_CULL_MODE_NONE);
    if (options.depthFormat != VK_FORMAT_UNDEFINED) {
        builder.Depth(options.depthFormat, false, false, VK_COMPARE_OP_ALWAYS);
    }
    return builder.Build();
}

Result<Gpu::Pipeline> BuildGrid(Gpu::Device& device, const RendererOptions& options) {
    const std::array set{Binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT)};
    Gpu::PipelineBuilder builder(device);
    builder.Vertex(GridVert())
        .Fragment(GridFrag())
        .Topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .SetLayout(set)
        .Color(options.colorFormat, Alpha())
        .Samples(options.samples)
        .Cull(VK_CULL_MODE_NONE);
    if (options.depthFormat != VK_FORMAT_UNDEFINED) {
        // The shader writes the plane's depth, so the test occludes the grid behind geometry.
        builder.Depth(options.depthFormat, true, false, VK_COMPARE_OP_LESS_OR_EQUAL);
    }
    return builder.Build();
}

Result<Gpu::Pipeline> BuildOverlay(Gpu::Device& device, const RendererOptions& options) {
    const std::array bindings{Gpu::VertexBinding{0, sizeof(Shader::OverlayVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    const std::array attributes{
        Gpu::VertexAttribute{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Shader::OverlayVertex, position)},
        Gpu::VertexAttribute{1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Shader::OverlayVertex, color)},
    };
    const std::array set{Binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT)};
    Gpu::PipelineBuilder builder(device);
    builder.Vertex(OverlayVert())
        .Fragment(OverlayFrag())
        .VertexInput(bindings, attributes)
        .Topology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST)
        .SetLayout(set)
        .Color(options.colorFormat, Alpha())
        .Samples(options.samples)
        .Cull(VK_CULL_MODE_NONE);
    if (options.depthFormat != VK_FORMAT_UNDEFINED) {
        // Bones draw over the mesh: no depth test, no depth write.
        builder.Depth(options.depthFormat, false, false, VK_COMPARE_OP_ALWAYS);
    }
    return builder.Build();
}

}

Result<ScenePipelines> BuildScenePipelines(Gpu::Device& device, const RendererOptions& options) {
    ScenePipelines out;

    auto opaque = BuildScene(device, options, BlendState{}, true, VK_COMPARE_OP_LESS);
    if (!opaque.ok()) {
        return Result<ScenePipelines>::Fail(opaque.error());
    }
    out.opaque = std::move(opaque.value());

    auto alpha = BuildScene(device, options, Alpha(), false, VK_COMPARE_OP_LESS_OR_EQUAL);
    if (!alpha.ok()) {
        return Result<ScenePipelines>::Fail(alpha.error());
    }
    out.alpha = std::move(alpha.value());

    auto additive = BuildScene(device, options, Additive(), false, VK_COMPARE_OP_LESS_OR_EQUAL);
    if (!additive.ok()) {
        return Result<ScenePipelines>::Fail(additive.error());
    }
    out.additive = std::move(additive.value());

    // Sky writes depth like the opaque pass and blends like the alpha pass.
    auto sky = BuildScene(device, options, Alpha(), true, VK_COMPARE_OP_LESS);
    if (!sky.ok()) {
        return Result<ScenePipelines>::Fail(sky.error());
    }
    out.sky = std::move(sky.value());

    auto background = BuildBackground(device, options);
    if (!background.ok()) {
        return Result<ScenePipelines>::Fail(background.error());
    }
    out.background = std::move(background.value());

    auto grid = BuildGrid(device, options);
    if (!grid.ok()) {
        return Result<ScenePipelines>::Fail(grid.error());
    }
    out.grid = std::move(grid.value());

    auto overlay = BuildOverlay(device, options);
    if (!overlay.ok()) {
        return Result<ScenePipelines>::Fail(overlay.error());
    }
    out.overlay = std::move(overlay.value());

    return Result<ScenePipelines>::Ok(std::move(out));
}

}
