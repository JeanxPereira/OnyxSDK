#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <volk.h>

#include "Onyx/Core/Result.h"
#include "Onyx/Gpu/Handles.h"

namespace Onyx::Gpu {

class Device;

struct VertexBinding {
    uint32_t binding = 0;
    uint32_t stride = 0;
    VkVertexInputRate rate = VK_VERTEX_INPUT_RATE_VERTEX;
};

struct VertexAttribute {
    uint32_t location = 0;
    uint32_t binding = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t offset = 0;
};

struct BlendState {
    bool enabled = false;
    VkBlendFactor srcColor = VK_BLEND_FACTOR_SRC_ALPHA;
    VkBlendFactor dstColor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendFactor srcAlpha = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstAlpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendOp colorOp = VK_BLEND_OP_ADD;
    VkBlendOp alphaOp = VK_BLEND_OP_ADD;
};

struct Pipeline {
    Unique<VkPipeline> pipeline;
    Unique<VkPipelineLayout> layout;
    std::vector<Unique<VkDescriptorSetLayout>> setLayouts;
};

// Builds a graphics pipeline for dynamic rendering through Device::PipelineCache(). Viewport and
// scissor are always dynamic. Build fails with gpu.vk when a stage is missing or the driver rejects
// the state; the SPIR-V is copied, so the spans only need to outlive the call that takes them.
class PipelineBuilder {
public:
    explicit PipelineBuilder(Device& device);

    PipelineBuilder& Vertex(std::span<const uint32_t> spirv);
    PipelineBuilder& Fragment(std::span<const uint32_t> spirv);
    PipelineBuilder& VertexInput(std::span<const VertexBinding> bindings, std::span<const VertexAttribute> attributes);
    PipelineBuilder& Topology(VkPrimitiveTopology topology);
    // Appends descriptor set N, where N is the number of earlier calls.
    PipelineBuilder& SetLayout(std::span<const VkDescriptorSetLayoutBinding> bindings);
    PipelineBuilder& PushConstants(VkShaderStageFlags stages, uint32_t size);
    // Appends a colour attachment.
    PipelineBuilder& Color(VkFormat format, const BlendState& blend = {});
    PipelineBuilder& Depth(VkFormat format, bool test, bool write, VkCompareOp compare);
    PipelineBuilder& Samples(VkSampleCountFlagBits samples);
    PipelineBuilder& Cull(VkCullModeFlags mode);
    // Adds dynamic states on top of viewport and scissor.
    PipelineBuilder& Dynamic(std::span<const VkDynamicState> states);

    Result<Pipeline> Build();

private:
    Device& m_device;
    std::vector<uint32_t> m_vertex;
    std::vector<uint32_t> m_fragment;
    std::vector<VertexBinding> m_bindings;
    std::vector<VertexAttribute> m_attributes;
    VkPrimitiveTopology m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    std::vector<std::vector<VkDescriptorSetLayoutBinding>> m_sets;
    VkShaderStageFlags m_pushStages = 0;
    uint32_t m_pushSize = 0;
    std::vector<VkFormat> m_colorFormats;
    std::vector<BlendState> m_blends;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    bool m_depthTest = false;
    bool m_depthWrite = false;
    VkCompareOp m_depthCompare = VK_COMPARE_OP_LESS;
    VkSampleCountFlagBits m_samples = VK_SAMPLE_COUNT_1_BIT;
    VkCullModeFlags m_cull = VK_CULL_MODE_NONE;
    std::vector<VkDynamicState> m_dynamic;
};

}
