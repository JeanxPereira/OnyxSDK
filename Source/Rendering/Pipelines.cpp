#include <Onyx/Rendering/Pipelines.h>

#include <Onyx/Domain/MeshVertex.h>

// Generated at build time (cmake/ShaderCompile.cmake's onyx_add_spirv,
// wired for the Onyx_Render target in the root CMakeLists.txt) into
// ${CMAKE_BINARY_DIR}/generated/shaders, which is on this target's
// PUBLIC include path. Never committed.
#include "scene_vert_spv.h"
#include "scene_frag_spv.h"
#include "grid_vert_spv.h"
#include "grid_frag_spv.h"
#include "background_vert_spv.h"
#include "background_frag_spv.h"
#include "overlay_vert_spv.h"
#include "overlay_frag_spv.h"

#include <array>
#include <cstddef>

namespace Onyx::Rendering {

namespace {

VkShaderModule CreateShaderModule(VkContext& ctx, const uint32_t* code, size_t codeSizeBytes,
                                   std::string& err) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = codeSizeBytes;
    info.pCode = code;

    VkShaderModule module = VK_NULL_HANDLE;
    VkResult vr = vkCreateShaderModule(ctx.Device(), &info, nullptr, &module);
    if (vr != VK_SUCCESS) {
        err = "vkCreateShaderModule failed (VkResult " + std::to_string(static_cast<int>(vr)) + ")";
        return VK_NULL_HANDLE;
    }
    return module;
}

// The GpuVertex attribute table (Include/Onyx/Domain/MeshVertex.h): a
// straight port of GpuMesh.cpp's glVertexAttribPointer calls, same
// locations, same offsets -- CPU-side mesh upload (a later task) can
// share the exact same struct across GL and Vulkan.
std::array<VkVertexInputAttributeDescription, 8> SceneVertexAttributes() {
    using Onyx::Domain::GpuVertex;
    // Field-by-field assignment (not aggregate positional init) to keep
    // every location/format/offset triple unambiguous and to sidestep any
    // narrowing-conversion question around offsetof()'s size_t result.
    auto attr = [](uint32_t location, VkFormat format, size_t offset) {
        VkVertexInputAttributeDescription a{};
        a.location = location;
        a.binding = 0;
        a.format = format;
        a.offset = static_cast<uint32_t>(offset);
        return a;
    };
    return {
        attr(0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, position)),
        attr(1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, normal)),
        attr(2, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuVertex, uv)),
        attr(3, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuVertex, color)),
        attr(4, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuVertex, uv1)),
        attr(5, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuVertex, boneWeights)),
        attr(6, VK_FORMAT_R32G32B32A32_UINT, offsetof(GpuVertex, boneIndices)),
        attr(7, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuVertex, tangent)),
    };
}

// Every pipeline in this file renders via dynamic rendering (no
// VkRenderPass) at the milestone's fixed color/depth formats and sample
// count -- see Pipelines.h's kColorFormat/kDepthFormat/kSampleCount.
// pColorAttachmentFormats points at the kColorFormat global directly
// (static storage duration, so the pointer stays valid for as long as
// the returned info is used) rather than a parameter -- every pipeline
// this file creates targets the one fixed format, so a parameter would
// only invite a dangling-reference footgun for zero flexibility gained.
VkPipelineRenderingCreateInfo SceneRenderingInfo() {
    VkPipelineRenderingCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    info.colorAttachmentCount = 1;
    info.pColorAttachmentFormats = &kColorFormat;
    info.depthAttachmentFormat = kDepthFormat;
    return info;
}

VkPipelineColorBlendAttachmentState BlendState(bool enable, VkBlendFactor srcFactor,
                                                VkBlendFactor dstFactor) {
    VkPipelineColorBlendAttachmentState state{};
    state.blendEnable = enable ? VK_TRUE : VK_FALSE;
    state.srcColorBlendFactor = srcFactor;
    state.dstColorBlendFactor = dstFactor;
    state.colorBlendOp = VK_BLEND_OP_ADD;
    // GL's glBlendFunc(src, dst) (not glBlendFuncSeparate) sets the same
    // factors for the alpha channel as for color -- SceneRenderer.cpp
    // never calls the Separate form, so this mirrors that exactly.
    state.srcAlphaBlendFactor = srcFactor;
    state.dstAlphaBlendFactor = dstFactor;
    state.alphaBlendOp = VK_BLEND_OP_ADD;
    state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    return state;
}

void DestroyShaderModules(VkContext& ctx, VkShaderModule vert, VkShaderModule frag) {
    if (vert != VK_NULL_HANDLE) vkDestroyShaderModule(ctx.Device(), vert, nullptr);
    if (frag != VK_NULL_HANDLE) vkDestroyShaderModule(ctx.Device(), frag, nullptr);
}

} // namespace

bool Pipelines::CreateScene(VkContext& ctx, ScenePipelines& out, std::string& err) {
    out = ScenePipelines{};

    // ── set 0: per-frame ─────────────────────────────────────────────
    VkDescriptorSetLayoutBinding frameBinding{};
    frameBinding.binding = 0;
    frameBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameBinding.descriptorCount = 1;
    frameBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo frameLayoutInfo{};
    frameLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    frameLayoutInfo.bindingCount = 1;
    frameLayoutInfo.pBindings = &frameBinding;

    VkResult vr = vkCreateDescriptorSetLayout(ctx.Device(), &frameLayoutInfo, nullptr,
                                               &out.frameSetLayout);
    if (vr != VK_SUCCESS) {
        err = "vkCreateDescriptorSetLayout (scene frame set) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        Destroy(ctx, out);
        return false;
    }

    // ── set 1: per-batch (material UBO + 6-slot role samplers + skinning
    // SSBO) — the brief's fixed descriptor scheme, consumed by T4-T8. ──
    std::array<VkDescriptorSetLayoutBinding, 3> batchBindings{};
    batchBindings[0].binding = 0;
    batchBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    batchBindings[0].descriptorCount = 1;
    batchBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    batchBindings[1].binding = 1;
    batchBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    batchBindings[1].descriptorCount = SceneRole::kCount;
    batchBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    batchBindings[2].binding = 2;
    batchBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    batchBindings[2].descriptorCount = 1;
    batchBindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo batchLayoutInfo{};
    batchLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    batchLayoutInfo.bindingCount = static_cast<uint32_t>(batchBindings.size());
    batchLayoutInfo.pBindings = batchBindings.data();

    vr = vkCreateDescriptorSetLayout(ctx.Device(), &batchLayoutInfo, nullptr, &out.batchSetLayout);
    if (vr != VK_SUCCESS) {
        err = "vkCreateDescriptorSetLayout (scene batch set) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        Destroy(ctx, out);
        return false;
    }

    // ── pipeline layout: set 0, set 1, + the 64B model-matrix push constant ──
    std::array<VkDescriptorSetLayout, 2> setLayouts{out.frameSetLayout, out.batchSetLayout};

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(glm::mat4);
    static_assert(sizeof(glm::mat4) == 64, "push constant must be exactly 64 bytes per the brief");

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    vr = vkCreatePipelineLayout(ctx.Device(), &layoutInfo, nullptr, &out.layout);
    if (vr != VK_SUCCESS) {
        err = "vkCreatePipelineLayout (scene) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        Destroy(ctx, out);
        return false;
    }

    // ── shader modules ───────────────────────────────────────────────
    using namespace Onyx::Rendering::Shaders;
    VkShaderModule vertModule =
        CreateShaderModule(ctx, kSceneVertSpv, kSceneVertSpvSize, err);
    if (vertModule == VK_NULL_HANDLE) {
        Destroy(ctx, out);
        return false;
    }
    VkShaderModule fragModule =
        CreateShaderModule(ctx, kSceneFragSpv, kSceneFragSpvSize, err);
    if (fragModule == VK_NULL_HANDLE) {
        vkDestroyShaderModule(ctx.Device(), vertModule, nullptr);
        Destroy(ctx, out);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // ── fixed state shared by every scene pipeline variant ──────────
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Onyx::Domain::GpuVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    auto attrs = SceneVertexAttributes();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // No face culling: SceneRenderer.cpp never calls glEnable(GL_CULL_FACE)
    // for the scene shader, so GL's default (culling off) applies to
    // every mesh it draws -- matched here exactly, not "reasonably".
    VkPipelineRasterizationStateCreateInfo rasterState{};
    rasterState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterState.polygonMode = VK_POLYGON_MODE_FILL;
    rasterState.cullMode = VK_CULL_MODE_NONE;
    rasterState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterState.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = kSampleCount;

    std::array<VkDynamicState, 2> dynStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynamicState.pDynamicStates = dynStates.data();

    VkPipelineRenderingCreateInfo renderingInfo = SceneRenderingInfo();

    // ── the four depth/blend-state variants (Pipelines.h's ScenePipelines
    // doc explains the mapping to GL's RenderSky/Pass 1/Pass 2 behavior) ──
    struct Variant {
        VkPipeline*      target;
        bool             depthWrite;
        VkCompareOp      depthCompare;
        VkPipelineColorBlendAttachmentState blend;
    };
    std::array<Variant, 4> variants{{
        {&out.opaque, true, VK_COMPARE_OP_LESS,
         BlendState(false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO)},
        {&out.blendNormal, false, VK_COMPARE_OP_LESS_OR_EQUAL,
         BlendState(true, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)},
        {&out.blendAdditive, false, VK_COMPARE_OP_LESS_OR_EQUAL,
         BlendState(true, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE)},
        // RenderSky (Source/Rendering/SceneRenderer.cpp:525-543): depth
        // write ON + LESS like opaque, but blended like blendNormal --
        // a combination neither of the other three provide.
        {&out.sky, true, VK_COMPARE_OP_LESS,
         BlendState(true, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)},
    }};

    for (auto& v : variants) {
        VkPipelineDepthStencilStateCreateInfo depthState{};
        depthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthState.depthTestEnable = VK_TRUE;
        depthState.depthWriteEnable = v.depthWrite ? VK_TRUE : VK_FALSE;
        depthState.depthCompareOp = v.depthCompare;

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &v.blend;

        VkGraphicsPipelineCreateInfo pipeInfo{};
        pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeInfo.pNext = &renderingInfo;
        pipeInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipeInfo.pStages = stages.data();
        pipeInfo.pVertexInputState = &vertexInput;
        pipeInfo.pInputAssemblyState = &inputAssembly;
        pipeInfo.pViewportState = &viewportState;
        pipeInfo.pRasterizationState = &rasterState;
        pipeInfo.pMultisampleState = &multisample;
        pipeInfo.pDepthStencilState = &depthState;
        pipeInfo.pColorBlendState = &colorBlend;
        pipeInfo.pDynamicState = &dynamicState;
        pipeInfo.layout = out.layout;

        vr = vkCreateGraphicsPipelines(ctx.Device(), VK_NULL_HANDLE, 1, &pipeInfo, nullptr, v.target);
        if (vr != VK_SUCCESS) {
            err = "vkCreateGraphicsPipelines (scene) failed (VkResult " +
                  std::to_string(static_cast<int>(vr)) + ")";
            DestroyShaderModules(ctx, vertModule, fragModule);
            Destroy(ctx, out);
            return false;
        }
    }

    DestroyShaderModules(ctx, vertModule, fragModule);
    return true;
}

void Pipelines::Destroy(VkContext& ctx, ScenePipelines& p) {
    if (p.opaque != VK_NULL_HANDLE) vkDestroyPipeline(ctx.Device(), p.opaque, nullptr);
    if (p.blendNormal != VK_NULL_HANDLE) vkDestroyPipeline(ctx.Device(), p.blendNormal, nullptr);
    if (p.blendAdditive != VK_NULL_HANDLE) vkDestroyPipeline(ctx.Device(), p.blendAdditive, nullptr);
    if (p.sky != VK_NULL_HANDLE) vkDestroyPipeline(ctx.Device(), p.sky, nullptr);
    if (p.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(ctx.Device(), p.layout, nullptr);
    if (p.batchSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx.Device(), p.batchSetLayout, nullptr);
    if (p.frameSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx.Device(), p.frameSetLayout, nullptr);
    p = ScenePipelines{};
}

bool Pipelines::CreateGrid(VkContext& ctx, GridPipeline& out, std::string& err) {
    out = GridPipeline{};

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    VkResult vr = vkCreateDescriptorSetLayout(ctx.Device(), &layoutInfo, nullptr, &out.setLayout);
    if (vr != VK_SUCCESS) {
        err = "vkCreateDescriptorSetLayout (grid) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        Destroy(ctx, out);
        return false;
    }

    VkPipelineLayoutCreateInfo pipeLayoutInfo{};
    pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeLayoutInfo.setLayoutCount = 1;
    pipeLayoutInfo.pSetLayouts = &out.setLayout;

    vr = vkCreatePipelineLayout(ctx.Device(), &pipeLayoutInfo, nullptr, &out.layout);
    if (vr != VK_SUCCESS) {
        err = "vkCreatePipelineLayout (grid) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        Destroy(ctx, out);
        return false;
    }

    using namespace Onyx::Rendering::Shaders;
    VkShaderModule vertModule = CreateShaderModule(ctx, kGridVertSpv, kGridVertSpvSize, err);
    if (vertModule == VK_NULL_HANDLE) {
        Destroy(ctx, out);
        return false;
    }
    VkShaderModule fragModule = CreateShaderModule(ctx, kGridFragSpv, kGridFragSpvSize, err);
    if (fragModule == VK_NULL_HANDLE) {
        vkDestroyShaderModule(ctx.Device(), vertModule, nullptr);
        Destroy(ctx, out);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // No vertex input: the fullscreen triangle is generated from
    // gl_VertexIndex (grid.vert), matching GridRenderer::Initialize's
    // empty-VAO GL setup exactly (no VBO, no attributes).
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterState{};
    rasterState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterState.polygonMode = VK_POLYGON_MODE_FILL;
    rasterState.cullMode = VK_CULL_MODE_NONE;
    rasterState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterState.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = kSampleCount;

    // Depth test on (LEQUAL, matching Viewport3D.cpp's glDepthFunc(GL_LEQUAL)
    // around its grid draw call — "Allow grid lines to render if exactly
    // coplanar"), write off (glDepthMask(GL_FALSE) there); the shader
    // still writes gl_FragDepth (grid.frag) so the depth TEST occludes
    // correctly against real geometry even though nothing is stored back.
    VkPipelineDepthStencilStateCreateInfo depthState{};
    depthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthState.depthTestEnable = VK_TRUE;
    depthState.depthWriteEnable = VK_FALSE;
    depthState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blend =
        BlendState(true, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blend;

    std::array<VkDynamicState, 2> dynStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynamicState.pDynamicStates = dynStates.data();

    VkPipelineRenderingCreateInfo renderingInfo = SceneRenderingInfo();

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.pNext = &renderingInfo;
    pipeInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipeInfo.pStages = stages.data();
    pipeInfo.pVertexInputState = &vertexInput;
    pipeInfo.pInputAssemblyState = &inputAssembly;
    pipeInfo.pViewportState = &viewportState;
    pipeInfo.pRasterizationState = &rasterState;
    pipeInfo.pMultisampleState = &multisample;
    pipeInfo.pDepthStencilState = &depthState;
    pipeInfo.pColorBlendState = &colorBlend;
    pipeInfo.pDynamicState = &dynamicState;
    pipeInfo.layout = out.layout;

    vr = vkCreateGraphicsPipelines(ctx.Device(), VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &out.pipeline);
    DestroyShaderModules(ctx, vertModule, fragModule);
    if (vr != VK_SUCCESS) {
        err = "vkCreateGraphicsPipelines (grid) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        Destroy(ctx, out);
        return false;
    }
    return true;
}

void Pipelines::Destroy(VkContext& ctx, GridPipeline& p) {
    if (p.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(ctx.Device(), p.pipeline, nullptr);
    if (p.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(ctx.Device(), p.layout, nullptr);
    if (p.setLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx.Device(), p.setLayout, nullptr);
    p = GridPipeline{};
}

bool Pipelines::CreateBackground(VkContext& ctx, BackgroundPipeline& out, std::string& err) {
    out = BackgroundPipeline{};

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    VkResult vr = vkCreateDescriptorSetLayout(ctx.Device(), &layoutInfo, nullptr, &out.setLayout);
    if (vr != VK_SUCCESS) {
        err = "vkCreateDescriptorSetLayout (background) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        Destroy(ctx, out);
        return false;
    }

    VkPipelineLayoutCreateInfo pipeLayoutInfo{};
    pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeLayoutInfo.setLayoutCount = 1;
    pipeLayoutInfo.pSetLayouts = &out.setLayout;

    vr = vkCreatePipelineLayout(ctx.Device(), &pipeLayoutInfo, nullptr, &out.layout);
    if (vr != VK_SUCCESS) {
        err = "vkCreatePipelineLayout (background) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        Destroy(ctx, out);
        return false;
    }

    using namespace Onyx::Rendering::Shaders;
    VkShaderModule vertModule =
        CreateShaderModule(ctx, kBackgroundVertSpv, kBackgroundVertSpvSize, err);
    if (vertModule == VK_NULL_HANDLE) {
        Destroy(ctx, out);
        return false;
    }
    VkShaderModule fragModule =
        CreateShaderModule(ctx, kBackgroundFragSpv, kBackgroundFragSpvSize, err);
    if (fragModule == VK_NULL_HANDLE) {
        vkDestroyShaderModule(ctx.Device(), vertModule, nullptr);
        Destroy(ctx, out);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // No vertex input, same reasoning as the grid pipeline.
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterState{};
    rasterState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterState.polygonMode = VK_POLYGON_MODE_FILL;
    rasterState.cullMode = VK_CULL_MODE_NONE;
    rasterState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterState.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = kSampleCount;

    // Depth test AND write both off -- matches
    // SceneRenderer::RenderBackground's glDisable(GL_DEPTH_TEST) +
    // glDepthMask(GL_FALSE) exactly (background.vert's divergence note:
    // the pass never interacts with the depth buffer in either API).
    VkPipelineDepthStencilStateCreateInfo depthState{};
    depthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthState.depthTestEnable = VK_FALSE;
    depthState.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blend =
        BlendState(false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blend;

    std::array<VkDynamicState, 2> dynStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynamicState.pDynamicStates = dynStates.data();

    VkPipelineRenderingCreateInfo renderingInfo = SceneRenderingInfo();

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.pNext = &renderingInfo;
    pipeInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipeInfo.pStages = stages.data();
    pipeInfo.pVertexInputState = &vertexInput;
    pipeInfo.pInputAssemblyState = &inputAssembly;
    pipeInfo.pViewportState = &viewportState;
    pipeInfo.pRasterizationState = &rasterState;
    pipeInfo.pMultisampleState = &multisample;
    pipeInfo.pDepthStencilState = &depthState;
    pipeInfo.pColorBlendState = &colorBlend;
    pipeInfo.pDynamicState = &dynamicState;
    pipeInfo.layout = out.layout;

    vr = vkCreateGraphicsPipelines(ctx.Device(), VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &out.pipeline);
    DestroyShaderModules(ctx, vertModule, fragModule);
    if (vr != VK_SUCCESS) {
        err = "vkCreateGraphicsPipelines (background) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        Destroy(ctx, out);
        return false;
    }
    return true;
}

void Pipelines::Destroy(VkContext& ctx, BackgroundPipeline& p) {
    if (p.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(ctx.Device(), p.pipeline, nullptr);
    if (p.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(ctx.Device(), p.layout, nullptr);
    if (p.setLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx.Device(), p.setLayout, nullptr);
    p = BackgroundPipeline{};
}

bool Pipelines::CreateOverlay(VkContext& ctx, OverlayPipeline& out, std::string& err) {
    out = OverlayPipeline{};

    // set 0, binding 0: OverlayUBO (view*proj only), vertex stage only --
    // overlay.frag is a pure passthrough and reads no descriptor.
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    VkResult vr = vkCreateDescriptorSetLayout(ctx.Device(), &layoutInfo, nullptr, &out.setLayout);
    if (vr != VK_SUCCESS) {
        err = "vkCreateDescriptorSetLayout (overlay) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        Destroy(ctx, out);
        return false;
    }

    VkPipelineLayoutCreateInfo pipeLayoutInfo{};
    pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeLayoutInfo.setLayoutCount = 1;
    pipeLayoutInfo.pSetLayouts = &out.setLayout;

    vr = vkCreatePipelineLayout(ctx.Device(), &pipeLayoutInfo, nullptr, &out.layout);
    if (vr != VK_SUCCESS) {
        err = "vkCreatePipelineLayout (overlay) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        Destroy(ctx, out);
        return false;
    }

    using namespace Onyx::Rendering::Shaders;
    VkShaderModule vertModule = CreateShaderModule(ctx, kOverlayVertSpv, kOverlayVertSpvSize, err);
    if (vertModule == VK_NULL_HANDLE) {
        Destroy(ctx, out);
        return false;
    }
    VkShaderModule fragModule = CreateShaderModule(ctx, kOverlayFragSpv, kOverlayFragSpvSize, err);
    if (fragModule == VK_NULL_HANDLE) {
        vkDestroyShaderModule(ctx.Device(), vertModule, nullptr);
        Destroy(ctx, out);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // OverlayVertex (Pipelines.h): world-space pos (location 0, vec3) +
    // per-vertex color (location 1, vec4) -- mirrors GL RenderSkeleton's
    // local `LineVert` struct exactly.
    VkVertexInputBindingDescription binding0{};
    binding0.binding = 0;
    binding0.stride = sizeof(OverlayVertex);
    binding0.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attrs{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = static_cast<uint32_t>(offsetof(OverlayVertex, pos));
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[1].offset = static_cast<uint32_t>(offsetof(OverlayVertex, color));

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding0;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions = attrs.data();

    // LINE_LIST: RenderSkeleton draws GL_LINES.
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterState{};
    rasterState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterState.polygonMode = VK_POLYGON_MODE_FILL;
    rasterState.cullMode = VK_CULL_MODE_NONE;
    rasterState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterState.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = kSampleCount;

    // Depth test AND write both off, matching RenderSkeleton's own
    // glDisable(GL_DEPTH_TEST) around its GL_LINES draw call exactly.
    VkPipelineDepthStencilStateCreateInfo depthState{};
    depthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthState.depthTestEnable = VK_FALSE;
    depthState.depthWriteEnable = VK_FALSE;

    // Blend on, SRC_ALPHA/ONE_MINUS_SRC_ALPHA -- matches RenderSkeleton's
    // glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA).
    VkPipelineColorBlendAttachmentState blend =
        BlendState(true, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blend;

    std::array<VkDynamicState, 2> dynStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynamicState.pDynamicStates = dynStates.data();

    VkPipelineRenderingCreateInfo renderingInfo = SceneRenderingInfo();

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.pNext = &renderingInfo;
    pipeInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipeInfo.pStages = stages.data();
    pipeInfo.pVertexInputState = &vertexInput;
    pipeInfo.pInputAssemblyState = &inputAssembly;
    pipeInfo.pViewportState = &viewportState;
    pipeInfo.pRasterizationState = &rasterState;
    pipeInfo.pMultisampleState = &multisample;
    pipeInfo.pDepthStencilState = &depthState;
    pipeInfo.pColorBlendState = &colorBlend;
    pipeInfo.pDynamicState = &dynamicState;
    pipeInfo.layout = out.layout;

    vr = vkCreateGraphicsPipelines(ctx.Device(), VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &out.pipeline);
    DestroyShaderModules(ctx, vertModule, fragModule);
    if (vr != VK_SUCCESS) {
        err = "vkCreateGraphicsPipelines (overlay) failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        Destroy(ctx, out);
        return false;
    }
    return true;
}

void Pipelines::Destroy(VkContext& ctx, OverlayPipeline& p) {
    if (p.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(ctx.Device(), p.pipeline, nullptr);
    if (p.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(ctx.Device(), p.layout, nullptr);
    if (p.setLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx.Device(), p.setLayout, nullptr);
    p = OverlayPipeline{};
}

} // namespace Onyx::Rendering
