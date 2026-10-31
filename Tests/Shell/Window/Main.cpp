#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <GLFW/glfw3.h>

#include <cstdio>

#include "Onyx/Gpu/Device.h"

int main(int argc, char** argv) {
    if (!glfwInit()) {
        std::puts("Shell.Window: glfwInit failed, skipping");
        return 77;
    }
    {
        uint32_t count = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&count);
        Onyx::Gpu::DeviceOptions options;
        options.validation = false;
        options.presentSupport = true;
        for (uint32_t i = 0; extensions && i < count; ++i) {
            options.instanceExtensions.push_back(extensions[i]);
        }
        auto probe = Onyx::Gpu::Device::Create(options);
        if (!probe.ok() && probe.error().code == "gpu.noDevice") {
            std::puts("Shell.Window: no Vulkan device, skipping");
            glfwTerminate();
            return 77;
        }
    }
    const int result = doctest::Context(argc, argv).run();
    glfwTerminate();
    return result;
}
