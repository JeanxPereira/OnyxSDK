#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <string>

#include "Onyx/Gpu/Device.h"
#include "Onyx/Shell/NativeWindow.h"
#include "Onyx/Shell/Presenter.h"

namespace {

// True when the machine can run the suite: a Vulkan device whose graphics queue presents to a window
// surface. A missing device or present support skips; any other failure is left to the cases.
bool CanPresent() {
    Onyx::Shell::WindowOptions windowOptions;
    windowOptions.title = "Onyx.Shell.Window.Probe";
    windowOptions.size = {64, 64};
    windowOptions.visible = false;
    auto window = Onyx::Shell::NativeWindow::Create(windowOptions);
    if (!window.ok()) {
        return true;
    }
    Onyx::Gpu::DeviceOptions options;
    options.validation = false;
    options.presentSupport = true;
    options.instanceExtensions = window.value()->RequiredInstanceExtensions();
    auto device = Onyx::Gpu::Device::Create(options);
    if (!device.ok()) {
        return device.error().code != "gpu.noDevice";
    }
    auto surface = window.value()->CreateSurface(device.value()->Instance());
    if (!surface.ok()) {
        return true;
    }
    bool presents = true;
    {
        auto presenter = Onyx::Shell::Presenter::Create(*device.value(), surface.value(), {64, 64});
        if (!presenter.ok()) {
            const std::string& code = presenter.error().code;
            presents = code != "gpu.surface" && code != "gpu.present";
        }
    }
    device.value()->InstanceFn().destroySurfaceKHR(device.value()->Instance(), surface.value(), nullptr);
    return presents;
}

}

int main(int argc, char** argv) {
    if (!glfwInit()) {
        std::puts("Shell.Window: glfwInit failed, skipping");
        return 77;
    }
    if (!CanPresent()) {
        std::puts("Shell.Window: no Vulkan device that presents to a window, skipping");
        glfwTerminate();
        return 77;
    }
    const int result = doctest::Context(argc, argv).run();
    glfwTerminate();
    return result;
}
