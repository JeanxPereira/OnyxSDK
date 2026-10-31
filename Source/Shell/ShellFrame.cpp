#include "Onyx/Shell/ShellFrame.h"

#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui.h>

#include <algorithm>

namespace Onyx::Shell {

namespace {

constexpr double kMinimisedWaitSeconds = 0.05;
constexpr glm::vec4 kClearColor{0.10f, 0.10f, 0.10f, 1.0f};

struct StepGuard {
    explicit StepGuard(bool& flag) : m_flag(flag) { m_flag = true; }
    ~StepGuard() { m_flag = false; }
    StepGuard(const StepGuard&) = delete;
    StepGuard& operator=(const StepGuard&) = delete;

    bool& m_flag;
};

}

ShellFrame::ShellFrame(NativeWindow& window, Gpu::Device& device, Presenter& presenter, Gpu::FrameRing& frames,
                       Ui::ImGuiVulkanBackend& backend, Ui::Theme& theme, Ui::FrameClock& clock)
    : m_window(window),
      m_device(device),
      m_presenter(presenter),
      m_frames(frames),
      m_backend(backend),
      m_theme(theme),
      m_clock(clock),
      m_lastTime(glfwGetTime()) {}

Result<bool> ShellFrame::Step(const std::function<void(Gpu::Frame&)>& drawUi) {
    using Out = Result<bool>;
    if (m_inStep) {
        return Out::Fail({"shell.frame", "Step was called from inside its own draw callback", 0});
    }
    StepGuard guard(m_inStep);
    if (m_window.ShouldClose()) {
        return Out::Ok(false);
    }

    const auto minimised = [this] {
        const glm::ivec2 size = m_window.FramebufferSize();
        return size.x <= 0 || size.y <= 0;
    };
    if (minimised()) {
        m_window.WaitEvents(kMinimisedWaitSeconds);
    } else if (m_clock.WantsContinuous()) {
        m_window.PollEvents();
    } else {
        const double remaining = m_clock.RemainingAnimation();
        m_window.WaitEvents(remaining > 0.0 ? (std::min)(remaining, m_idleWaitCap) : m_idleWaitCap);
    }
    if (m_window.ShouldClose()) {
        return Out::Ok(false);
    }

    if (minimised()) {
        m_clock.BeginFrame(glfwGetTime());
        return Out::Ok(true);
    }
    const glm::ivec2 framebuffer = m_window.FramebufferSize();
    const VkExtent2D extent = m_presenter.Extent();
    if (m_presenter.NeedsRecreate() || extent.width != static_cast<uint32_t>(framebuffer.x) ||
        extent.height != static_cast<uint32_t>(framebuffer.y)) {
        auto resized = m_presenter.Resize(framebuffer);
        if (!resized.ok()) {
            return Out::Fail(resized.error());
        }
    }

    const double now = glfwGetTime();
    const float dt = static_cast<float>((std::max)(0.0, now - m_lastTime));
    m_lastTime = now;
    m_clock.BeginFrame(now);
    m_theme.Update(dt);
    m_theme.Commit();
    if (m_theme.AtlasDirty()) {
        if (m_fontBuilder) {
            m_fontBuilder();
        }
        m_backend.RebuildFonts();
        m_theme.ClearAtlasDirty();
        auto fault = m_backend.Fault();
        if (!fault.ok()) {
            return Out::Fail(fault.error());
        }
    }

    auto begun = m_frames.Begin();
    if (!begun.ok()) {
        return Out::Fail(begun.error());
    }
    Gpu::Frame& frame = *begun.value();

    auto acquired = m_presenter.Acquire(frame);
    if (!acquired.ok()) {
        auto ended = m_frames.End(frame);
        Error error = acquired.error();
        if (!ended.ok()) {
            error.message += "; ending the frame also failed: " + ended.error().code + ": " + ended.error().message;
        }
        return Out::Fail(std::move(error));
    }
    if (!acquired.value().has_value()) {
        auto ended = m_frames.End(frame);
        if (!ended.ok()) {
            return Out::Fail(ended.error());
        }
        const glm::ivec2 current = m_window.FramebufferSize();
        if (current.x > 0 && current.y > 0) {
            auto resized = m_presenter.Resize(current);
            if (!resized.ok()) {
                return Out::Fail(resized.error());
            }
        }
        return Out::Ok(true);
    }
    const Gpu::SwapImage image = *acquired.value();

    m_backend.NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    if (drawUi) {
        drawUi(frame);
    }
    TitleBarRect caption = m_window.GetTitleBarRect();
    caption.blocked = ImGui::IsAnyItemHovered() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    m_window.SetTitleBarRect(caption);
    ImGui::Render();

    m_presenter.BeginRendering(frame.Cmd(), image, kClearColor);
    m_backend.Record(frame.Cmd(), ImGui::GetDrawData());
    m_presenter.EndRendering(frame.Cmd(), image);

    auto ended = m_frames.End(frame);
    if (!ended.ok()) {
        return Out::Fail(ended.error());
    }
    auto presented = m_presenter.Present(ended.value(), image);
    auto fault = m_backend.Fault();
    if (!presented.ok()) {
        Error error = presented.error();
        if (!fault.ok()) {
            error.message += "; the backend also reported " + fault.error().code + ": " + fault.error().message;
        }
        return Out::Fail(std::move(error));
    }
    if (!fault.ok()) {
        return Out::Fail(fault.error());
    }
    return Out::Ok(true);
}

}
