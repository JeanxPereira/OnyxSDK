#pragma once

#include <doctest/doctest.h>

#include <cstdlib>
#include <memory>
#include <string_view>

#include "Onyx/Gpu/Device.h"
#include "ValidationLayer.h"

namespace Onyx {

inline std::unique_ptr<Gpu::Device> CreateTestDevice(const Gpu::DeviceOptions& options) {
    auto device = Gpu::Device::Create(options);
    REQUIRE_MESSAGE(device.ok(), device.error().code << ": " << device.error().message);
    if (options.validation) {
        RequireValidationLayer(*device.value());
    }
    return std::move(device.value());
}

inline std::unique_ptr<Gpu::Device> CreateTestDevice(bool validation = true) {
    Gpu::DeviceOptions options;
    options.validation = validation;
    return CreateTestDevice(options);
}

}
