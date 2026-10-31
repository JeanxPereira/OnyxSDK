#include <doctest/doctest.h>

#include <vector>

#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/FrameRing.h"
#include "Onyx/Gpu/Upload.h"
#include "Support/TestDevice.h"

using namespace Onyx;

TEST_CASE("FrameRing reuses a slot only after its submit completed") {
    auto device = CreateTestDevice();
    auto ring = Gpu::FrameRing::Create(*device, {.framesInFlight = 2, .transientBytes = 4096});
    REQUIRE(ring.ok());
    CHECK(ring.value()->FramesInFlight() == 2);
    for (int i = 0; i < 6; ++i) {
        auto f = ring.value()->Begin();
        REQUIRE(f.ok());
        CHECK(f.value()->Index() == uint32_t(i % 2));
        auto t = f.value()->Allocate(1024, 256);
        REQUIRE(t.ok());
        CHECK(t.value().offset == 0);
        CHECK(t.value().mapped != nullptr);
        auto v = ring.value()->End(*f.value());
        REQUIRE(v.ok());
        CHECK(v.value() == Gpu::FenceValue(i + 1));
        CHECK(ring.value()->LastValue() == v.value());
    }
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("Frame::Allocate honours alignment and reports exhaustion") {
    auto device = CreateTestDevice();
    auto ring = Gpu::FrameRing::Create(*device, {.framesInFlight = 2, .transientBytes = 4096});
    REQUIRE(ring.ok());
    auto f = ring.value()->Begin();
    REQUIRE(f.ok());
    auto a = f.value()->Allocate(100, 256);
    auto b = f.value()->Allocate(100, 256);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(a.value().offset == 0);
    CHECK(b.value().offset == 256);
    CHECK(a.value().buffer == b.value().buffer);
    auto c = f.value()->Allocate(4096, 16);
    REQUIRE_FALSE(c.ok());
    CHECK(c.error().code == "gpu.transientFull");
    CHECK(ring.value()->End(*f.value()).ok());
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("Frame waits on an upload value before its commands") {
    auto device = CreateTestDevice();
    auto up = Gpu::UploadContext::Create(*device);
    REQUIRE(up.ok());
    auto buf = up.value()->Upload(std::vector<std::byte>(256), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    REQUIRE(buf.ok());
    auto value = up.value()->Flush();
    auto ring = Gpu::FrameRing::Create(*device);
    REQUIRE(ring.ok());
    auto f = ring.value()->Begin();
    REQUIRE(f.ok());
    f.value()->AddWait(value);
    auto v = ring.value()->End(*f.value());
    REQUIRE(v.ok());
    CHECK(v.value() > value);
    CHECK(device->GraphicsQueue().Wait(v.value()).ok());
    CHECK(device->ValidationMessageCount() == 0);
}

TEST_CASE("FrameRing refuses a second Begin while a frame is open") {
    auto device = CreateTestDevice();
    auto ring = Gpu::FrameRing::Create(*device);
    REQUIRE(ring.ok());
    auto f = ring.value()->Begin();
    REQUIRE(f.ok());
    auto g = ring.value()->Begin();
    REQUIRE_FALSE(g.ok());
    CHECK(g.error().code == "gpu.frame");
    CHECK(ring.value()->End(*f.value()).ok());
}
