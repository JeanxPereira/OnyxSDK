#include <doctest/doctest.h>

#include <Spirv/TriangleFrag.h>
#include <Spirv/TriangleVert.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <span>

#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/Handles.h"
#include "Onyx/Gpu/Pipeline.h"
#include "Onyx/Gpu/RenderTarget.h"
#include "Support/TestDevice.h"

using namespace Onyx;

namespace {

// Records into a one-off command buffer and submits it without waiting.
template <class Record>
Gpu::FenceValue SubmitOnce(Gpu::Device& device, Record&& record) {
    const auto& fn = device.Fn();
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = device.GraphicsQueue().Family();
    VkCommandPool rawPool = VK_NULL_HANDLE;
    REQUIRE(fn.vkCreateCommandPool(device.Handle(), &poolInfo, nullptr, &rawPool) == VK_SUCCESS);
    Gpu::Unique<VkCommandPool> pool(&device, rawPool);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = rawPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    REQUIRE(fn.vkAllocateCommandBuffers(device.Handle(), &allocInfo, &cmd) == VK_SUCCESS);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    REQUIRE(fn.vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);
    record(cmd);
    REQUIRE(fn.vkEndCommandBuffer(cmd) == VK_SUCCESS);

    std::array cmds{cmd};
    auto value = device.GraphicsQueue().Submit({.commandBuffers = cmds});
    REQUIRE(value.ok());
    return value.value();
}

bool NearlyEqual(std::byte actual, int expected) {
    return std::abs(static_cast<int>(actual) - expected) <= 1;
}

}

TEST_CASE("RenderTarget clears to the requested colour and reads back top-down") {
    auto device = CreateTestDevice();
    auto rt = Gpu::RenderTarget::Create(*device, {.extent = {64, 64}});
    REQUIRE(rt.ok());
    CHECK(rt.value()->Generation() == 0);
    CHECK(rt.value()->Extent().width == 64);

    auto v = SubmitOnce(*device, [&](VkCommandBuffer cmd) {
        rt.value()->Begin(cmd, {0.25f, 0.5f, 0.75f, 1.0f});
        rt.value()->End(cmd);
    });
    auto px = Gpu::ReadbackColor(*device, *rt.value(), v);
    REQUIRE(px.ok());
    REQUIRE(px.value().size() == 64 * 64 * 4);
    bool allMatch = true;
    for (size_t i = 0; i < px.value().size(); i += 4) {
        allMatch = allMatch && NearlyEqual(px.value()[i + 0], 64) && NearlyEqual(px.value()[i + 1], 128) &&
                   NearlyEqual(px.value()[i + 2], 191) && NearlyEqual(px.value()[i + 3], 255);
    }
    CHECK(allMatch);
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("Triangle draw covers the target and depth test is active") {
    auto device = CreateTestDevice();
    auto rt = Gpu::RenderTarget::Create(*device, {.extent = {64, 48}});
    REQUIRE(rt.ok());
    auto pipeline = Gpu::PipelineBuilder(*device)
                        .Vertex({Spirv::kTriangleVert, Spirv::kTriangleVertWords})
                        .Fragment({Spirv::kTriangleFrag, Spirv::kTriangleFragWords})
                        .Color(VK_FORMAT_R8G8B8A8_UNORM)
                        .Depth(VK_FORMAT_D32_SFLOAT, true, true, VK_COMPARE_OP_LESS)
                        .Samples(VK_SAMPLE_COUNT_4_BIT)
                        .Build();
    REQUIRE(pipeline.ok());

    auto v = SubmitOnce(*device, [&](VkCommandBuffer cmd) {
        rt.value()->Begin(cmd, {0.0f, 0.0f, 1.0f, 1.0f});
        device->Fn().vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.value().pipeline.Get());
        device->Fn().vkCmdDraw(cmd, 3, 1, 0, 0);
        // A second draw at the same depth fails the LESS test and must leave the first colour intact.
        device->Fn().vkCmdDraw(cmd, 3, 1, 0, 0);
        rt.value()->End(cmd);
    });
    auto px = Gpu::ReadbackColor(*device, *rt.value(), v);
    REQUIRE(px.ok());
    REQUIRE(px.value().size() == 64 * 48 * 4);
    bool allRed = true;
    for (size_t i = 0; i < px.value().size(); i += 4) {
        allRed = allRed && NearlyEqual(px.value()[i + 0], 255) && NearlyEqual(px.value()[i + 1], 0) &&
                 NearlyEqual(px.value()[i + 2], 0) && NearlyEqual(px.value()[i + 3], 255);
    }
    CHECK(allRed);
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("Resize under validation retires the old images and the next frame is clean") {
    auto device = CreateTestDevice();
    auto rt = Gpu::RenderTarget::Create(*device, {.extent = {64, 64}});
    REQUIRE(rt.ok());

    SubmitOnce(*device, [&](VkCommandBuffer cmd) {
        rt.value()->Begin(cmd, {1.0f, 0.0f, 0.0f, 1.0f});
        rt.value()->End(cmd);
    });
    REQUIRE(rt.value()->Resize({128, 96}).ok());
    CHECK(rt.value()->Generation() == 1);
    CHECK(rt.value()->Extent().width == 128);
    CHECK(rt.value()->Extent().height == 96);

    auto v = SubmitOnce(*device, [&](VkCommandBuffer cmd) {
        rt.value()->Begin(cmd, {0.0f, 1.0f, 0.0f, 1.0f});
        rt.value()->End(cmd);
    });
    REQUIRE(device->GraphicsQueue().Wait(v).ok());
    CHECK(device->Retire().Pending() > 0);
    device->Retire().Collect(device->GraphicsQueue().Completed());
    CHECK(device->ValidationMessageCount() == 0);

    auto zero = rt.value()->Resize({0, 0});
    REQUIRE_FALSE(zero.ok());
    CHECK(zero.error().code == "gpu.extent");
    CHECK(rt.value()->Generation() == 1);

    auto created = Gpu::RenderTarget::Create(*device, {.extent = {0, 16}});
    REQUIRE_FALSE(created.ok());
    CHECK(created.error().code == "gpu.extent");
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("RenderTarget without multisampling or depth renders into the resolve image") {
    auto device = CreateTestDevice();
    auto rt = Gpu::RenderTarget::Create(
        *device, {.extent = {16, 16}, .depthFormat = VK_FORMAT_UNDEFINED, .samples = VK_SAMPLE_COUNT_1_BIT});
    REQUIRE(rt.ok());
    auto v = SubmitOnce(*device, [&](VkCommandBuffer cmd) {
        rt.value()->Begin(cmd, {1.0f, 1.0f, 0.0f, 1.0f});
        rt.value()->End(cmd);
    });
    auto px = Gpu::ReadbackColor(*device, *rt.value(), v);
    REQUIRE(px.ok());
    CHECK(NearlyEqual(px.value()[0], 255));
    CHECK(NearlyEqual(px.value()[1], 255));
    CHECK(NearlyEqual(px.value()[2], 0));
    CHECK(device->ValidationMessageCount() == 0);
}
