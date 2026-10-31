#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>

#include "Onyx/Gpu/Device.h"

int main(int argc, char** argv) {
    {
        Onyx::Gpu::DeviceOptions options;
        options.validation = false;
        auto probe = Onyx::Gpu::Device::Create(options);
        if (!probe.ok() && probe.error().code == "gpu.noDevice") {
            std::puts("Ui.Gpu: no Vulkan device, skipping");
            return 77;
        }
    }
    return doctest::Context(argc, argv).run();
}
