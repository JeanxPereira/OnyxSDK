#pragma once

#include <functional>
#include <memory>

#include "Onyx/Core/Result.h"
#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/FrameRing.h"
#include "Onyx/Shell/GlfwPlatform.h"
#include "Onyx/Shell/NativeWindow.h"
#include "Onyx/Shell/Presenter.h"
#include "Onyx/Ui/FrameClock.h"
#include "Onyx/Ui/ImGuiVulkanBackend.h"
#include "Onyx/Ui/Theme.h"

// Error codes: shell.frame (re-entrant Step); Gpu codes and ui.backend.init (a backend fault) pass through.
namespace Onyx::Shell {

// The frame loop: owns no resources, only the order of operations. Step requires a live GlfwPlatform
// (Onyx/Shell/GlfwPlatform.h) bound to the window and the current ImGui context. Not thread-safe.
class ShellFrame {
public:
    ShellFrame(NativeWindow& window, Gpu::Device& device, Presenter& presenter, Gpu::FrameRing& frames,
               Ui::ImGuiVulkanBackend& backend, Ui::Theme& theme, Ui::FrameClock& clock);

    // The longest an idle step waits for events when the clock has no animation pending.
    void SetIdleWaitCap(double seconds) { m_idleWaitCap = seconds; }

    // Called when the theme marked the font atlas dirty, after the theme committed and before ImGui
    // starts the frame: it rebuilds the atlas from the theme's applied font. The backend textures are
    // rebuilt right after.
    void SetFontBuilder(std::function<void()> builder) { m_fontBuilder = std::move(builder); }

    // One frame. Pacing: events are waited for (at most the clock's remaining animation, or the idle cap)
    // unless the clock wants continuous rendering, in which case they are polled. Then the theme
    // advances and commits outside the ImGui frame, the font atlas is rebuilt when the theme marked it
    // dirty, ImGui starts a frame, `drawUi` runs with the open GPU frame (uploads and waits go on it),
    // and the draw data is recorded into the acquired swapchain image and presented. The swapchain is
    // recreated when it is stale or the framebuffer size changed; an out-of-date acquire recreates it
    // and skips the frame; a zero-sized framebuffer (minimised) skips the frame
    // without rendering and waits briefly for events instead of spinning.
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
    std::function<void()> m_fontBuilder;
    double m_lastTime = 0.0;
    double m_idleWaitCap = 1.0;
    bool m_inStep = false;
};

}
