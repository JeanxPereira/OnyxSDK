#pragma once

#include <functional>
#include <memory>

#include "Onyx/Core/Result.h"
#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/FrameRing.h"
#include "Onyx/Shell/NativeWindow.h"
#include "Onyx/Shell/Presenter.h"
#include "Onyx/Ui/FrameClock.h"
#include "Onyx/Ui/ImGuiVulkanBackend.h"
#include "Onyx/Ui/Theme.h"

// Error codes: shell.frame (re-entrant Step); Gpu and ui.backend.init codes pass through.
namespace Onyx::Shell {

// The GLFW platform backend of ImGui bound to a window. It binds to the ImGui context that is current
// when Create runs and must be destroyed while that context is still current and before the window.
class GlfwPlatform {
public:
    static Result<std::unique_ptr<GlfwPlatform>> Create(NativeWindow& window);
    ~GlfwPlatform();

    GlfwPlatform(const GlfwPlatform&) = delete;
    GlfwPlatform& operator=(const GlfwPlatform&) = delete;

private:
    GlfwPlatform() = default;
};

// The frame loop: owns no resources, only the order of operations. Not thread-safe.
class ShellFrame {
public:
    ShellFrame(NativeWindow& window, Gpu::Device& device, Presenter& presenter, Gpu::FrameRing& frames,
               Ui::ImGuiVulkanBackend& backend, Ui::Theme& theme, Ui::FrameClock& clock);

    // One frame. Pacing: events are waited for (at most the clock's remaining animation, or one second)
    // unless the clock wants continuous rendering, in which case they are polled. Then the theme
    // advances and commits outside the ImGui frame, the font atlas is rebuilt when the theme marked it
    // dirty, ImGui starts a frame, `drawUi` runs with the open GPU frame (uploads and waits go on it),
    // and the draw data is recorded into the acquired swapchain image and presented. The swapchain is
    // recreated when it is stale or the framebuffer size changed; an out-of-date acquire recreates it
    // and skips the frame; a zero-sized framebuffer skips the frame without rendering.
    // Returns false once the window should close. Calling Step from `drawUi` fails with shell.frame.
    Result<bool> Step(const std::function<void(Gpu::Frame&)>& drawUi);

private:
    NativeWindow& m_window;
    Gpu::Device& m_device;
    Presenter& m_presenter;
    Gpu::FrameRing& m_frames;
    Ui::ImGuiVulkanBackend& m_backend;
    Ui::Theme& m_theme;
    Ui::FrameClock& m_clock;
    double m_lastTime = 0.0;
    bool m_inStep = false;
};

}
