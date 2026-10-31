#include <doctest/doctest.h>

#include <Spirv/TriangleFrag.h>
#include <Spirv/TriangleVert.h>

#include <array>
#include <span>

#include "Onyx/Gpu/Descriptors.h"
#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/Pipeline.h"
#include "Support/TestDevice.h"

using namespace Onyx;

namespace {

std::span<const uint32_t> VertexWords() {
    return {Spirv::kTriangleVert, Spirv::kTriangleVertWords};
}

std::span<const uint32_t> FragmentWords() {
    return {Spirv::kTriangleFrag, Spirv::kTriangleFragWords};
}

}

TEST_CASE("PipelineBuilder builds the triangle pipeline against the pipeline cache") {
    auto device = CreateTestDevice();
    auto p = Gpu::PipelineBuilder(*device)
                 .Vertex(VertexWords())
                 .Fragment(FragmentWords())
                 .Color(VK_FORMAT_R8G8B8A8_UNORM)
                 .Depth(VK_FORMAT_D32_SFLOAT, true, true, VK_COMPARE_OP_LESS)
                 .Samples(VK_SAMPLE_COUNT_4_BIT)
                 .Build();
    REQUIRE(p.ok());
    CHECK(p.value().pipeline.Get() != VK_NULL_HANDLE);
    CHECK(p.value().layout.Get() != VK_NULL_HANDLE);
    CHECK(p.value().setLayouts.empty());
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("PipelineBuilder reports a shader stage mismatch as gpu.vk, not a crash") {
    auto device = CreateTestDevice();
    auto fragmentOnly = Gpu::PipelineBuilder(*device)
                            .Fragment(FragmentWords())
                            .Color(VK_FORMAT_R8G8B8A8_UNORM)
                            .Build();
    REQUIRE_FALSE(fragmentOnly.ok());
    CHECK(fragmentOnly.error().code == "gpu.vk");

    auto vertexOnly = Gpu::PipelineBuilder(*device).Vertex(VertexWords()).Color(VK_FORMAT_R8G8B8A8_UNORM).Build();
    REQUIRE_FALSE(vertexOnly.ok());
    CHECK(vertexOnly.error().code == "gpu.vk");
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("PipelineBuilder creates descriptor set layouts and push constant ranges") {
    auto device = CreateTestDevice();
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    auto p = Gpu::PipelineBuilder(*device)
                 .Vertex(VertexWords())
                 .Fragment(FragmentWords())
                 .SetLayout({&binding, 1})
                 .PushConstants(VK_SHADER_STAGE_VERTEX_BIT, 16)
                 .Color(VK_FORMAT_R8G8B8A8_UNORM, {.enabled = true})
                 .Cull(VK_CULL_MODE_BACK_BIT)
                 .Build();
    REQUIRE(p.ok());
    REQUIRE(p.value().setLayouts.size() == 1);
    CHECK(p.value().setLayouts[0].Get() != VK_NULL_HANDLE);
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("DescriptorPool allocates sets from a layout and recycles them on Reset") {
    auto device = CreateTestDevice();
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    auto p = Gpu::PipelineBuilder(*device)
                 .Vertex(VertexWords())
                 .Fragment(FragmentWords())
                 .SetLayout({&binding, 1})
                 .Color(VK_FORMAT_R8G8B8A8_UNORM)
                 .Build();
    REQUIRE(p.ok());

    std::array sizes{VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}};
    auto pool = Gpu::DescriptorPool::Create(*device, sizes, 2);
    REQUIRE(pool.ok());
    VkDescriptorSetLayout layout = p.value().setLayouts[0].Get();
    auto a = pool.value().Allocate(layout);
    auto b = pool.value().Allocate(layout);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(a.value() != b.value());
    auto c = pool.value().Allocate(layout);
    CHECK_FALSE(c.ok());
    CHECK(c.error().code == "gpu.vk");
    pool.value().Reset();
    CHECK(pool.value().Allocate(layout).ok());

    auto sampler = Gpu::CreateSampler(*device, {.filter = VK_FILTER_NEAREST, .mipmaps = false});
    REQUIRE(sampler.ok());
    CHECK(sampler.value().Get() != VK_NULL_HANDLE);

    auto anisotropic = Gpu::CreateSampler(*device, {.maxAnisotropy = 16.0f});
    REQUIRE(anisotropic.ok());
    CHECK(anisotropic.value().Get() != VK_NULL_HANDLE);
    CHECK(device->ValidationMessageCount() == 0);
}
