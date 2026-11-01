#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <thread>

#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/FrameRing.h"
#include "Onyx/Gpu/Handles.h"
#include "Support/TestDevice.h"
#include "TempDir.h"

using namespace Onyx;

TEST_CASE("Device::Create picks a device, enables the 1.3 features and reports zero validation messages") {
    auto device = CreateTestDevice();
    CHECK_FALSE(device->Info().deviceName.empty());
    CHECK(device->Info().apiVersion >= VK_API_VERSION_1_3);
    CHECK(device->Info().robustBufferAccess);
    CHECK(device->ValidationMessageCount() == 0);
    CHECK(device->SubmitCount() == 0);
}

TEST_CASE("Queue::Submit of an empty submit advances the timeline and the submit counter") {
    auto device = CreateTestDevice();
    auto v = device->GraphicsQueue().Submit({});
    REQUIRE(v.ok());
    CHECK(v.value() == 1);
    CHECK(device->GraphicsQueue().Wait(v.value()).ok());
    CHECK(device->GraphicsQueue().Completed() >= 1);
    CHECK(device->SubmitCount() == 1);
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("Queue::Lock holds off a Submit from another thread until it is released") {
    auto device = CreateTestDevice();
    Gpu::Queue& queue = device->GraphicsQueue();
    std::atomic<bool> submitted{false};
    std::thread submitter;
    {
        const auto lock = queue.Lock();
        submitter = std::thread([&] {
            auto v = queue.Submit({});
            CHECK(v.ok());
            submitted = true;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK_FALSE(submitted.load());
        CHECK(device->SubmitCount() == 0);
    }
    submitter.join();
    CHECK(submitted.load());
    CHECK(device->SubmitCount() == 1);
    CHECK(queue.Wait(queue.LastSubmitted()).ok());
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("Unique handles retire at the last submitted value and Collect destroys them") {
    auto device = CreateTestDevice();
    {
        auto buf = Gpu::CreateBuffer(*device, 256, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, Gpu::MemoryUsage::HostToDevice);
        REQUIRE(buf.ok());
        CHECK(buf.value().mapped != nullptr);
    }
    CHECK(device->Retire().Pending() == 1);
    auto v = device->GraphicsQueue().Submit({});
    REQUIRE(v.ok());
    device->GraphicsQueue().Wait(v.value());
    device->Retire().Collect(device->GraphicsQueue().Completed());
    CHECK(device->Retire().Pending() == 0);
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("Images retire their view and allocation with the handle") {
    auto device = CreateTestDevice();
    {
        Gpu::ImageDesc desc;
        desc.extent = {16, 16};
        desc.format = VK_FORMAT_R8G8B8A8_UNORM;
        desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        auto image = Gpu::CreateImage(*device, desc);
        REQUIRE(image.ok());
        CHECK(image.value().view);
    }
    CHECK(device->Retire().Pending() == 2);
    device->Retire().Drain();
    CHECK(device->Retire().Pending() == 0);
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("Device::Adopt wraps a host device and refuses a missing feature") {
    auto own = CreateTestDevice();
    Gpu::ExternalDevice ext{own->Instance(),
                            own->PhysicalDevice(),
                            own->Handle(),
                            own->GraphicsQueue().Handle(),
                            own->GraphicsQueue().Family(),
                            vkGetInstanceProcAddr,
                            vkGetDeviceProcAddr,
                            {true, true, true},
                            {}};
    auto adopted = Gpu::Device::Adopt(ext);
    REQUIRE(adopted.ok());
    CHECK(adopted.value()->Info().adopted);
    CHECK_FALSE(adopted.value()->Info().robustBufferAccess);
    auto v = adopted.value()->GraphicsQueue().Submit({});
    REQUIRE(v.ok());
    CHECK(adopted.value()->GraphicsQueue().Wait(v.value()).ok());
    adopted.value().reset();
    auto hostSubmit = own->GraphicsQueue().Submit({});
    REQUIRE(hostSubmit.ok());
    CHECK(own->GraphicsQueue().Wait(hostSubmit.value()).ok());
    CHECK_FALSE(own->GraphicsQueue().Fault().has_value());
    CHECK(own->ValidationMessageCount() == 0);

    ext.enabled.timelineSemaphore = false;
    auto refused = Gpu::Device::Adopt(ext);
    REQUIRE_FALSE(refused.ok());
    CHECK(refused.error().code == "gpu.feature");
}

TEST_CASE("Two owned devices coexist and dispatch through their own tables") {
    auto a = CreateTestDevice();
    auto b = CreateTestDevice();
    auto va = a->GraphicsQueue().Submit({});
    auto vb = b->GraphicsQueue().Submit({});
    REQUIRE(va.ok());
    REQUIRE(vb.ok());
    CHECK(a->GraphicsQueue().Wait(va.value()).ok());
    CHECK(b->GraphicsQueue().Wait(vb.value()).ok());
    CHECK(a->Handle() != b->Handle());
    CHECK(b->ValidationMessageCount() == 0);
    b.reset();
    CHECK(a->ValidationMessageCount() == 0);
    a.reset();
}

TEST_CASE("Pipeline cache file is written on destruction and loaded on the next Create") {
    Onyx::Test::TempDir scope("gpu-cache");
    auto path = scope / "cache.bin";
    {
        Gpu::DeviceOptions o;
        o.pipelineCachePath = path;
        auto d = CreateTestDevice(o);
        CHECK(d->PipelineCache() != VK_NULL_HANDLE);
        CHECK(d->ValidationMessageCount() == 0);
    }
    CHECK(std::filesystem::exists(path));
    {
        Gpu::DeviceOptions o;
        o.pipelineCachePath = path;
        auto d = CreateTestDevice(o);
        CHECK(d->PipelineCache() != VK_NULL_HANDLE);
        CHECK(d->ValidationMessageCount() == 0);
    }
}

namespace {

// Stands in for a host loader that resolves no Vulkan 1.3 core entry points above the instance's API
// version: vkQueueSubmit2 is withheld, everything else is forwarded.
PFN_vkGetDeviceProcAddr g_realGetDeviceProcAddr = nullptr;

PFN_vkVoidFunction VKAPI_CALL WithholdSubmit2(VkDevice device, const char* name) {
    if (std::strcmp(name, "vkQueueSubmit2") == 0) {
        return nullptr;
    }
    return g_realGetDeviceProcAddr(device, name);
}

}

TEST_CASE("Device::Adopt refuses a host that does not resolve the 1.3 core entry points") {
    auto own = CreateTestDevice();
    g_realGetDeviceProcAddr = vkGetDeviceProcAddr;
    Gpu::ExternalDevice ext{own->Instance(),
                            own->PhysicalDevice(),
                            own->Handle(),
                            own->GraphicsQueue().Handle(),
                            own->GraphicsQueue().Family(),
                            vkGetInstanceProcAddr,
                            WithholdSubmit2,
                            {true, true, true},
                            {}};
    auto refused = Gpu::Device::Adopt(ext);
    REQUIRE_FALSE(refused.ok());
    CHECK(refused.error().code == "gpu.feature");
    CHECK(refused.error().message.find("vkQueueSubmit2") != std::string::npos);
    CHECK(own->ValidationMessageCount() == 0);
}

TEST_CASE("A frame waits on a host timeline semaphore at a value") {
    auto device = CreateTestDevice();
    const auto& fn = device->Fn();

    VkSemaphoreTypeCreateInfo typeInfo{};
    typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = &typeInfo;
    VkSemaphore hostTimeline = VK_NULL_HANDLE;
    REQUIRE(fn.vkCreateSemaphore(device->Handle(), &info, nullptr, &hostTimeline) == VK_SUCCESS);

    Gpu::SemaphoreSignal signal;
    signal.semaphore = hostTimeline;
    signal.value = 5;
    Gpu::SubmitDesc hostSubmit;
    hostSubmit.signals = std::span<const Gpu::SemaphoreSignal>(&signal, 1);
    auto signalled = device->GraphicsQueue().Submit(hostSubmit);
    REQUIRE(signalled.ok());

    auto ring = Gpu::FrameRing::Create(*device);
    REQUIRE(ring.ok());
    auto frame = ring.value()->Begin();
    REQUIRE(frame.ok());
    frame.value()->AddWait(hostTimeline, 5, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    auto ended = ring.value()->End(*frame.value());
    REQUIRE(ended.ok());
    CHECK(device->GraphicsQueue().Wait(ended.value()).ok());

    uint64_t value = 0;
    REQUIRE(fn.vkGetSemaphoreCounterValue(device->Handle(), hostTimeline, &value) == VK_SUCCESS);
    CHECK(value == 5);
    CHECK_FALSE(device->GraphicsQueue().Fault().has_value());
    CHECK_MESSAGE(device->ValidationMessageCount() == 0, device->LastValidationMessage());
    fn.vkDestroySemaphore(device->Handle(), hostTimeline, nullptr);
}
