#include "Onyx/Gpu/Pipeline.h"

#include <algorithm>
#include <array>

#include "Onyx/Gpu/Device.h"
#include "VkError.h"

namespace Onyx::Gpu {

namespace {

// A shader module only has to live until vkCreateGraphicsPipelines returns, so it is destroyed directly.
class ShaderModule {
public:
    ShaderModule(Device& device, std::span<const uint32_t> words) : m_device(device) {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = words.size() * sizeof(uint32_t);
        info.pCode = words.data();
        m_result = device.Fn().vkCreateShaderModule(device.Handle(), &info, nullptr, &m_module);
        if (m_result != VK_SUCCESS) {
            m_module = VK_NULL_HANDLE;
        }
    }
    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;
    ~ShaderModule() {
        if (m_module != VK_NULL_HANDLE) {
            m_device.Fn().vkDestroyShaderModule(m_device.Handle(), m_module, nullptr);
        }
    }

    VkShaderModule Get() const { return m_module; }
    VkResult Status() const { return m_result; }

private:
    Device& m_device;
    VkShaderModule m_module = VK_NULL_HANDLE;
    VkResult m_result = VK_SUCCESS;
};

bool HasStencil(VkFormat format) {
    switch (format) {
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
    case VK_FORMAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

}

PipelineBuilder::PipelineBuilder(Device& device) : m_device(device) {}

PipelineBuilder& PipelineBuilder::Vertex(std::span<const uint32_t> spirv) {
    m_vertex.assign(spirv.begin(), spirv.end());
    return *this;
}

PipelineBuilder& PipelineBuilder::Fragment(std::span<const uint32_t> spirv) {
    m_fragment.assign(spirv.begin(), spirv.end());
    return *this;
}

PipelineBuilder& PipelineBuilder::VertexInput(std::span<const VertexBinding> bindings,
                                              std::span<const VertexAttribute> attributes) {
    m_bindings.assign(bindings.begin(), bindings.end());
    m_attributes.assign(attributes.begin(), attributes.end());
    return *this;
}

PipelineBuilder& PipelineBuilder::Topology(VkPrimitiveTopology topology) {
    m_topology = topology;
    return *this;
}

PipelineBuilder& PipelineBuilder::SetLayout(std::span<const VkDescriptorSetLayoutBinding> bindings) {
    m_sets.emplace_back(bindings.begin(), bindings.end());
    return *this;
}

PipelineBuilder& PipelineBuilder::PushConstants(VkShaderStageFlags stages, uint32_t size) {
    m_pushStages = stages;
    m_pushSize = size;
    return *this;
}

PipelineBuilder& PipelineBuilder::Color(VkFormat format, const BlendState& blend) {
    m_colorFormats.push_back(format);
    m_blends.push_back(blend);
    return *this;
}

PipelineBuilder& PipelineBuilder::Depth(VkFormat format, bool test, bool write, VkCompareOp compare) {
    m_depthFormat = format;
    m_depthTest = test;
    m_depthWrite = write;
    m_depthCompare = compare;
    return *this;
}

PipelineBuilder& PipelineBuilder::Samples(VkSampleCountFlagBits samples) {
    m_samples = samples;
    return *this;
}

PipelineBuilder& PipelineBuilder::Cull(VkCullModeFlags mode) {
    m_cull = mode;
    return *this;
}

PipelineBuilder& PipelineBuilder::Dynamic(std::span<const VkDynamicState> states) {
    m_dynamic.insert(m_dynamic.end(), states.begin(), states.end());
    return *this;
}

Result<Pipeline> PipelineBuilder::Build() {
    if (m_vertex.empty() || m_fragment.empty()) {
        return Result<Pipeline>::Fail({"gpu.vk", "a graphics pipeline needs both a vertex and a fragment stage", 0});
    }
    const auto& fn = m_device.Fn();
    VkDevice device = m_device.Handle();

    Pipeline out;
    std::vector<VkDescriptorSetLayout> rawSetLayouts;
    for (const auto& bindings : m_sets) {
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = static_cast<uint32_t>(bindings.size());
        info.pBindings = bindings.data();
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        VkResult r = fn.vkCreateDescriptorSetLayout(device, &info, nullptr, &layout);
        if (r != VK_SUCCESS) {
            return Result<Pipeline>::Fail(VkFail("gpu.vk", "vkCreateDescriptorSetLayout", r));
        }
        out.setLayouts.emplace_back(&m_device, layout);
        rawSetLayouts.push_back(layout);
    }

    VkPushConstantRange pushRange{m_pushStages, 0, m_pushSize};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(rawSetLayouts.size());
    layoutInfo.pSetLayouts = rawSetLayouts.data();
    if (m_pushSize > 0) {
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
    }
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkResult r = fn.vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);
    if (r != VK_SUCCESS) {
        return Result<Pipeline>::Fail(VkFail("gpu.vk", "vkCreatePipelineLayout", r));
    }
    out.layout = Unique<VkPipelineLayout>(&m_device, pipelineLayout);

    ShaderModule vertex(m_device, m_vertex);
    if (vertex.Status() != VK_SUCCESS) {
        return Result<Pipeline>::Fail(VkFail("gpu.vk", "vkCreateShaderModule (vertex)", vertex.Status()));
    }
    ShaderModule fragment(m_device, m_fragment);
    if (fragment.Status() != VK_SUCCESS) {
        return Result<Pipeline>::Fail(VkFail("gpu.vk", "vkCreateShaderModule (fragment)", fragment.Status()));
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex.Get();
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment.Get();
    stages[1].pName = "main";

    std::vector<VkVertexInputBindingDescription> bindingDescs;
    for (const auto& b : m_bindings) {
        bindingDescs.push_back({b.binding, b.stride, b.rate});
    }
    std::vector<VkVertexInputAttributeDescription> attributeDescs;
    for (const auto& a : m_attributes) {
        attributeDescs.push_back({a.location, a.binding, a.format, a.offset});
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescs.size());
    vertexInput.pVertexBindingDescriptions = bindingDescs.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescs.size());
    vertexInput.pVertexAttributeDescriptions = attributeDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = m_topology;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = m_cull;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = m_samples;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = m_depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = m_depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = m_depthCompare;

    std::vector<VkPipelineColorBlendAttachmentState> attachments;
    for (const auto& blend : m_blends) {
        VkPipelineColorBlendAttachmentState a{};
        a.blendEnable = blend.enabled ? VK_TRUE : VK_FALSE;
        a.srcColorBlendFactor = blend.srcColor;
        a.dstColorBlendFactor = blend.dstColor;
        a.colorBlendOp = blend.colorOp;
        a.srcAlphaBlendFactor = blend.srcAlpha;
        a.dstAlphaBlendFactor = blend.dstAlpha;
        a.alphaBlendOp = blend.alphaOp;
        a.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;
        attachments.push_back(a);
    }
    VkPipelineColorBlendStateCreateInfo blendState{};
    blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.attachmentCount = static_cast<uint32_t>(attachments.size());
    blendState.pAttachments = attachments.data();

    std::vector<VkDynamicState> dynamic{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    for (VkDynamicState state : m_dynamic) {
        if (std::find(dynamic.begin(), dynamic.end(), state) == dynamic.end()) {
            dynamic.push_back(state);
        }
    }
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamic.size());
    dynamicState.pDynamicStates = dynamic.data();

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = static_cast<uint32_t>(m_colorFormats.size());
    rendering.pColorAttachmentFormats = m_colorFormats.data();
    rendering.depthAttachmentFormat = m_depthFormat;
    rendering.stencilAttachmentFormat = HasStencil(m_depthFormat) ? m_depthFormat : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = static_cast<uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depthStencil;
    info.pColorBlendState = &blendState;
    info.pDynamicState = &dynamicState;
    info.layout = pipelineLayout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    r = fn.vkCreateGraphicsPipelines(device, m_device.PipelineCache(), 1, &info, nullptr, &pipeline);
    if (r != VK_SUCCESS) {
        return Result<Pipeline>::Fail(VkFail("gpu.vk", "vkCreateGraphicsPipelines", r));
    }
    out.pipeline = Unique<VkPipeline>(&m_device, pipeline);
    return Result<Pipeline>::Ok(std::move(out));
}

}
