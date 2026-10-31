#include <doctest/doctest.h>

#include <array>
#include <cstring>
#include <vector>

#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/Handles.h"
#include "Onyx/Gpu/Swapchain.h"
#include "Support/TestDevice.h"

using namespace Onyx;

namespace {

// Valid once any Device has been created in the process, because that initialises volk's loader.
bool InstanceExtensionListed(const char* name) {
    uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> props(count);
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data()) != VK_SUCCESS) {
        return false;
    }
    for (const auto& p : props) {
        if (std::strcmp(p.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

}

TEST_CASE("Swapchain creates against a headless surface when the extension exists") {
    {
        auto probe = CreateTestDevice(false);
        if (!InstanceExtensionListed(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME)) {
            MESSAGE("VK_EXT_headless_surface is not available on this loader; the swapchain path is not exercised");
            return;
        }
    }

    Gpu::DeviceOptions options;
    options.presentSupport = true;
    options.instanceExtensions.push_back(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
    auto device = CreateTestDevice(options);

    auto createSurface = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
        vkGetInstanceProcAddr(device->Instance(), "vkCreateHeadlessSurfaceEXT"));
    REQUIRE(createSurface != nullptr);
    VkHeadlessSurfaceCreateInfoEXT surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    REQUIRE(createSurface(device->Instance(), &surfaceInfo, nullptr, &surface) == VK_SUCCESS);

    {
        auto chain = Gpu::Swapchain::Create(*device, {.surface = surface, .extent = {256, 256}});
        REQUIRE_MESSAGE(chain.ok(), chain.error().code << ": " << chain.error().message);
        auto& sc = *chain.value();
        CHECK(sc.ImageCount() >= 1);
        CHECK(sc.Extent().width > 0);

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore rawSem = VK_NULL_HANDLE;
        REQUIRE(device->Fn().vkCreateSemaphore(device->Handle(), &semInfo, nullptr, &rawSem) == VK_SUCCESS);
        Gpu::Unique<VkSemaphore> acquired(device.get(), rawSem);

        auto image = sc.Acquire(rawSem);
        REQUIRE(image.ok());
        REQUIRE(image.value().has_value());
        CHECK(image.value()->image != VK_NULL_HANDLE);
        CHECK(image.value()->view != VK_NULL_HANDLE);

        // Present requires PRESENT_SRC, so a one-off submit transitions the acquired image first.
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = device->GraphicsQueue().Family();
        VkCommandPool rawPool = VK_NULL_HANDLE;
        REQUIRE(device->Fn().vkCreateCommandPool(device->Handle(), &poolInfo, nullptr, &rawPool) == VK_SUCCESS);
        Gpu::Unique<VkCommandPool> pool(device.get(), rawPool);
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = rawPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        REQUIRE(device->Fn().vkAllocateCommandBuffers(device->Handle(), &allocInfo, &cmd) == VK_SUCCESS);
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        REQUIRE(device->Fn().vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.value()->image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        device->Fn().vkCmdPipelineBarrier2(cmd, &dep);
        REQUIRE(device->Fn().vkEndCommandBuffer(cmd) == VK_SUCCESS);

        VkSemaphoreCreateInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore rawDone = VK_NULL_HANDLE;
        REQUIRE(device->Fn().vkCreateSemaphore(device->Handle(), &renderInfo, nullptr, &rawDone) == VK_SUCCESS);
        Gpu::Unique<VkSemaphore> done(device.get(), rawDone);

        std::array cmds{cmd};
        std::array waits{Gpu::SemaphoreWait{rawSem, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT}};
        std::array signals{Gpu::SemaphoreSignal{rawDone, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT}};
        auto submitted = device->GraphicsQueue().Submit({.commandBuffers = cmds, .waits = waits, .signals = signals});
        REQUIRE(submitted.ok());

        std::array present{rawDone};
        auto presented = sc.Present(present, image.value()->index);
        CHECK(presented.ok());

        REQUIRE(device->GraphicsQueue().Wait(submitted.value()).ok());
        auto recreated = sc.Recreate({320, 240});
        REQUIRE_MESSAGE(recreated.ok(), recreated.error().code << ": " << recreated.error().message);
        CHECK(sc.Extent().width > 0);
        CHECK_FALSE(sc.NeedsRecreate());

        auto zero = sc.Recreate({0, 0});
        REQUIRE_FALSE(zero.ok());
        CHECK(zero.error().code == "gpu.extent");
    }

    device->InstanceFn().destroySurfaceKHR(device->Instance(), surface, nullptr);
    CHECK(device->ValidationMessageCount() == 0);
}
