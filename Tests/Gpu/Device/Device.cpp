#include <doctest/doctest.h>

#include <filesystem>

#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/Handles.h"
#include "Support/TestDevice.h"

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
    auto path = std::filesystem::temp_directory_path() / "onyx-gpu-cache.bin";
    std::filesystem::remove(path);
    {
        Gpu::DeviceOptions o;
        o.pipelineCachePath = path;
        auto d = Gpu::Device::Create(o);
        REQUIRE(d.ok());
        CHECK(d.value()->PipelineCache() != VK_NULL_HANDLE);
        CHECK(d.value()->ValidationMessageCount() == 0);
    }
    CHECK(std::filesystem::exists(path));
    {
        Gpu::DeviceOptions o;
        o.pipelineCachePath = path;
        auto d = Gpu::Device::Create(o);
        REQUIRE(d.ok());
        CHECK(d.value()->PipelineCache() != VK_NULL_HANDLE);
        CHECK(d.value()->ValidationMessageCount() == 0);
    }
    std::filesystem::remove(path);
}
