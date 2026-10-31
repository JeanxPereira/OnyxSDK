#pragma once

#include <doctest/doctest.h>

#include <memory>

#include "Onyx/Gpu/Device.h"

namespace Onyx {

inline std::unique_ptr<Gpu::Device> CreateTestDevice(bool validation = true) {
    Gpu::DeviceOptions options;
    options.validation = validation;
    auto device = Gpu::Device::Create(options);
    REQUIRE_MESSAGE(device.ok(), device.error().code << ": " << device.error().message);
    return std::move(device.value());
}

}
