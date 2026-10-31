#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec2.hpp>
#include <volk.h>

#include "Onyx/Core/Result.h"

struct GLFWwindow;

// Error codes:
//   shell.window   GLFW could not initialise or create the window
//   shell.surface  the Vulkan surface could not be created for the window
//   shell.frame    ShellFrame::Step was re-entered from its own draw callback
namespace Onyx::Shell {

struct WindowOptions {
    std::string title;
    glm::ivec2 size{1280, 800};
    // Win32: a frameless window that keeps native snapping and resizing, with the caption area given
    // by SetTitleBarRect. Elsewhere: an undecorated GLFW window.
    bool borderless = true;
    bool visible = true;
};

// The caption area of a borderless window in client coordinates, republished by the frame every frame.
// `blocked` is true while an interactive item or a popup is under the cursor, which keeps the caption
// from swallowing the click.
struct TitleBarRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool blocked = false;
};

// One GLFW window. glfwInit is reference counted across live windows (the one process-wide piece GLFW
// forces); the last window to close terminates GLFW. Main thread only.
class NativeWindow {
public:
    static Result<std::unique_ptr<NativeWindow>> Create(const WindowOptions& options);

    NativeWindow(const NativeWindow&) = delete;
    NativeWindow& operator=(const NativeWindow&) = delete;
    ~NativeWindow();

    GLFWwindow* Handle() const { return m_window; }
    glm::ivec2 FramebufferSize() const;
    float ContentScale() const;
    bool ShouldClose() const;
    void SetShouldClose(bool close);

    void PollEvents();
    // Blocks until an event arrives or the timeout passes.
    void WaitEvents(double timeoutSeconds);
    void SetTitle(std::string_view title);

    void SetDropHandler(std::function<void(std::vector<std::filesystem::path>)> handler);
    void SetResizeHandler(std::function<void(glm::ivec2)> handler);
    // Feeds the paths, as UTF-8, through the same callback GLFW invokes for a drop.
    void SimulateDrop(std::vector<std::filesystem::path> paths);

    // Test hook: reports this size from FramebufferSize instead of the real one; nullopt restores it.
    void ForceFramebufferSize(std::optional<glm::ivec2> size) { m_forcedSize = size; }

    void SetTitleBarRect(const TitleBarRect& rect) { m_titleBar = rect; }
    const TitleBarRect& GetTitleBarRect() const { return m_titleBar; }

    // The surface belongs to the caller, who destroys it before the window.
    Result<VkSurfaceKHR> CreateSurface(VkInstance instance);
    // The instance extensions the window system needs.
    std::vector<const char*> RequiredInstanceExtensions() const;

private:
    explicit NativeWindow(const WindowOptions& options) : m_options(options) {}

    void InstallPlatform();
    void RemovePlatform();
    void DispatchDrop(std::vector<std::filesystem::path> paths);
    void DispatchResize(glm::ivec2 size);

    friend struct NativeWindowAccess;

    WindowOptions m_options;
    GLFWwindow* m_window = nullptr;
    TitleBarRect m_titleBar;
    std::optional<glm::ivec2> m_forcedSize;
    std::function<void(std::vector<std::filesystem::path>)> m_dropHandler;
    std::function<void(glm::ivec2)> m_resizeHandler;
    // Win32 only: the window procedure GLFW installed, restored on destruction.
    std::intptr_t m_previousProc = 0;
};

}
