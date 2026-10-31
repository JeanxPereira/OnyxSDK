#pragma once

#include <doctest/doctest.h>

#include <imgui.h>

#include <cstdlib>
#include <memory>
#include <string_view>

#include "Onyx/Core/Services/Settings.h"
#include "Onyx/Gpu/Device.h"
#include "ValidationLayer.h"
#include "Onyx/Gpu/FrameRing.h"
#include "Onyx/Shell/NativeWindow.h"
#include "Onyx/Shell/Presenter.h"
#include "Onyx/Shell/ShellFrame.h"
#include "Onyx/Ui/FrameClock.h"
#include "Onyx/Ui/ImGuiVulkanBackend.h"
#include "Onyx/Ui/Theme.h"

namespace Onyx {

// A hidden window with the whole present stack over it. Members are declared in construction order and
// destroyed in reverse by the destructor, which also owns the ImGui context and the surface.
struct WindowFixture {
    std::unique_ptr<Shell::NativeWindow> window;
    std::unique_ptr<Gpu::Device> device;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    std::unique_ptr<Shell::Presenter> presenter;
    std::unique_ptr<Gpu::FrameRing> frames;
    ImGuiContext* imgui = nullptr;
    std::unique_ptr<Shell::GlfwPlatform> platform;
    std::unique_ptr<Ui::ImGuiVulkanBackend> backend;
    Services::Settings settings = Services::Settings::Empty();
    std::unique_ptr<Ui::Theme> theme;
    Ui::FrameClock clock;
    std::unique_ptr<Shell::ShellFrame> shell;

    WindowFixture() = default;
    WindowFixture(const WindowFixture&) = delete;
    WindowFixture& operator=(const WindowFixture&) = delete;

    ~WindowFixture() {
        if (device) {
            device->GraphicsQueue().WaitIdle();
        }
        shell.reset();
        theme.reset();
        if (imgui) {
            ImGui::SetCurrentContext(imgui);
        }
        backend.reset();
        platform.reset();
        if (imgui) {
            ImGui::DestroyContext(imgui);
        }
        frames.reset();
        presenter.reset();
        if (surface != VK_NULL_HANDLE && device) {
            device->InstanceFn().destroySurfaceKHR(device->Instance(), surface, nullptr);
        }
        device.reset();
        window.reset();
    }
};

// Builds the fixture in place: it owns an ImGui context and so cannot be returned by value.
inline std::unique_ptr<WindowFixture> MakeWindowFixture(bool borderless = true,
                                                        glm::ivec2 size = {640, 360}) {
    auto fx = std::make_unique<WindowFixture>();

    Shell::WindowOptions windowOptions;
    windowOptions.title = "Onyx.Shell.Window";
    windowOptions.size = size;
    windowOptions.borderless = borderless;
    windowOptions.visible = false;
    auto window = Shell::NativeWindow::Create(windowOptions);
    REQUIRE_MESSAGE(window.ok(), window.error().code << ": " << window.error().message);
    fx->window = std::move(window.value());

    Gpu::DeviceOptions deviceOptions;
    deviceOptions.validation = true;
    deviceOptions.presentSupport = true;
    deviceOptions.instanceExtensions = fx->window->RequiredInstanceExtensions();
    auto device = Gpu::Device::Create(deviceOptions);
    REQUIRE_MESSAGE(device.ok(), device.error().code << ": " << device.error().message);
    fx->device = std::move(device.value());
    RequireValidationLayer(*fx->device);

    auto surface = fx->window->CreateSurface(fx->device->Instance());
    REQUIRE_MESSAGE(surface.ok(), surface.error().code << ": " << surface.error().message);
    fx->surface = surface.value();

    auto presenter = Shell::Presenter::Create(*fx->device, fx->surface, fx->window->FramebufferSize());
    REQUIRE_MESSAGE(presenter.ok(), presenter.error().code << ": " << presenter.error().message);
    fx->presenter = std::move(presenter.value());

    auto frames = Gpu::FrameRing::Create(*fx->device);
    REQUIRE_MESSAGE(frames.ok(), frames.error().code << ": " << frames.error().message);
    fx->frames = std::move(frames.value());

    IMGUI_CHECKVERSION();
    fx->imgui = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().Fonts->AddFontDefault();

    auto platform = Shell::GlfwPlatform::Create(*fx->window);
    REQUIRE_MESSAGE(platform.ok(), platform.error().code << ": " << platform.error().message);
    fx->platform = std::move(platform.value());

    Ui::BackendOptions backendOptions;
    backendOptions.colorFormat = fx->presenter->Format();
    backendOptions.imageCount = fx->presenter->ImageCount();
    auto backend = Ui::ImGuiVulkanBackend::Create(*fx->device, backendOptions);
    REQUIRE_MESSAGE(backend.ok(), backend.error().code << ": " << backend.error().message);
    fx->backend = std::move(backend.value());

    fx->theme = std::make_unique<Ui::Theme>(fx->settings);
    fx->shell = std::make_unique<Shell::ShellFrame>(*fx->window, *fx->device, *fx->presenter, *fx->frames,
                                                    *fx->backend, *fx->theme, fx->clock);
    return fx;
}

}
