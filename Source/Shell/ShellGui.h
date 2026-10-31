#pragma once

#include <memory>
#include <string>

#include <imgui.h>
#include <volk.h>

#include "Onyx/Core/Result.h"
#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/FrameRing.h"
#include "Onyx/Gpu/Upload.h"
#include "Onyx/Shell/GlfwPlatform.h"
#include "Onyx/Shell/NativeWindow.h"
#include "Onyx/Shell/Presenter.h"
#include "Onyx/Shell/Shell.h"
#include "Onyx/Shell/ShellFrame.h"
#include "Onyx/Ui/FrameClock.h"
#include "Onyx/Ui/ImGuiVulkanBackend.h"
#include "Onyx/Ui/TexturePool.h"
#include "Onyx/Ui/Theme.h"

namespace Onyx::Shell {

// Everything a running window needs, in construction order; the destructor releases it in reverse
// with the queue idle first. The texture pool must go before the panels that registered textures in
// it are gone, which is why Shell empties its PanelHost before dropping this.
struct GuiStack {
    std::unique_ptr<NativeWindow> window;
    std::unique_ptr<Gpu::Device> device;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    std::unique_ptr<Presenter> presenter;
    std::unique_ptr<Gpu::FrameRing> frames;
    ImGuiContext* imgui = nullptr;
    std::string iniPath;
    bool freshLayout = false;
    std::unique_ptr<GlfwPlatform> platform;
    std::unique_ptr<Ui::ImGuiVulkanBackend> backend;
    std::unique_ptr<Gpu::UploadContext> upload;
    std::unique_ptr<Ui::TexturePool> textures;
    std::unique_ptr<ShellFrame> frame;

    // Creates the window, device, presenter, ImGui context and renderer for the options. The theme
    // and clock belong to the Shell and must outlive the stack.
    static Result<std::unique_ptr<GuiStack>> Create(const ShellOptions& options, Ui::Theme& theme,
                                                    Ui::FrameClock& clock);

    GuiStack() = default;
    ~GuiStack();
    GuiStack(const GuiStack&) = delete;
    GuiStack& operator=(const GuiStack&) = delete;

    void WaitIdle();
};

}
