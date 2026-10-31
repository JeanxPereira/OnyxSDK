#include "Onyx/Shell/NativeWindow.h"

#include <GLFW/glfw3.h>

#include <mutex>
#include <string>
#include <utility>

namespace Onyx::Shell {

namespace {

// glfwInit and glfwTerminate are process-wide, so the live windows share one reference count.
std::mutex g_glfwMutex;
int g_glfwUsers = 0;

Result<void> AcquireGlfw() {
    std::lock_guard lock(g_glfwMutex);
    if (g_glfwUsers == 0 && glfwInit() != GLFW_TRUE) {
        const char* description = nullptr;
        glfwGetError(&description);
        return Result<void>::Fail({"shell.window", std::string("glfwInit failed: ") + (description ? description : "unknown"), 0});
    }
    ++g_glfwUsers;
    return Result<void>::Ok();
}

void ReleaseGlfw() {
    std::lock_guard lock(g_glfwMutex);
    if (g_glfwUsers > 0 && --g_glfwUsers == 0) {
        glfwTerminate();
    }
}

}

struct NativeWindowAccess {
    static NativeWindow* From(GLFWwindow* window) {
        return static_cast<NativeWindow*>(glfwGetWindowUserPointer(window));
    }

    static void OnDrop(GLFWwindow* window, int count, const char** paths) {
        NativeWindow* self = From(window);
        if (self == nullptr) {
            return;
        }
        std::vector<std::filesystem::path> list;
        list.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            const std::string_view utf8(paths[i]);
            list.emplace_back(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
        }
        self->DispatchDrop(std::move(list));
    }

    static void OnFramebufferSize(GLFWwindow* window, int width, int height) {
        if (NativeWindow* self = From(window)) {
            self->DispatchResize({width, height});
        }
    }
};

Result<std::unique_ptr<NativeWindow>> NativeWindow::Create(const WindowOptions& options) {
    using Out = Result<std::unique_ptr<NativeWindow>>;
    if (options.size.x <= 0 || options.size.y <= 0) {
        return Out::Fail({"shell.window", "the window size must be positive", 0});
    }
    auto init = AcquireGlfw();
    if (!init.ok()) {
        return Out::Fail(init.error());
    }
    std::unique_ptr<NativeWindow> window(new NativeWindow(options));

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, options.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, options.borderless ? GLFW_FALSE : GLFW_TRUE);
    window->m_window = glfwCreateWindow(options.size.x, options.size.y, options.title.c_str(), nullptr, nullptr);
    if (window->m_window == nullptr) {
        const char* description = nullptr;
        glfwGetError(&description);
        return Out::Fail({"shell.window", std::string("glfwCreateWindow failed: ") + (description ? description : "unknown"), 0});
    }
    glfwSetWindowUserPointer(window->m_window, window.get());
    glfwSetDropCallback(window->m_window, &NativeWindowAccess::OnDrop);
    glfwSetFramebufferSizeCallback(window->m_window, &NativeWindowAccess::OnFramebufferSize);
    window->InstallPlatform();
    return Out::Ok(std::move(window));
}

NativeWindow::~NativeWindow() {
    if (m_window != nullptr) {
        RemovePlatform();
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    ReleaseGlfw();
}

glm::ivec2 NativeWindow::FramebufferSize() const {
    if (m_forcedSize) {
        return *m_forcedSize;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    return {width, height};
}

float NativeWindow::ContentScale() const {
    float x = 1.0f;
    float y = 1.0f;
    glfwGetWindowContentScale(m_window, &x, &y);
    return x;
}

bool NativeWindow::ShouldClose() const { return glfwWindowShouldClose(m_window) == GLFW_TRUE; }

void NativeWindow::SetShouldClose(bool close) { glfwSetWindowShouldClose(m_window, close ? GLFW_TRUE : GLFW_FALSE); }

void NativeWindow::PollEvents() { glfwPollEvents(); }

void NativeWindow::WaitEvents(double timeoutSeconds) {
    if (timeoutSeconds <= 0.0) {
        glfwPollEvents();
        return;
    }
    glfwWaitEventsTimeout(timeoutSeconds);
}

void NativeWindow::SetTitle(std::string_view title) {
    const std::string copy(title);
    glfwSetWindowTitle(m_window, copy.c_str());
}

void NativeWindow::SetDropHandler(std::function<void(std::vector<std::filesystem::path>)> handler) {
    m_dropHandler = std::move(handler);
}

void NativeWindow::SetResizeHandler(std::function<void(glm::ivec2)> handler) { m_resizeHandler = std::move(handler); }

void NativeWindow::SimulateDrop(std::vector<std::filesystem::path> paths) {
    std::vector<std::string> utf8;
    utf8.reserve(paths.size());
    for (const auto& path : paths) {
        const std::u8string encoded = path.u8string();
        utf8.emplace_back(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    }
    std::vector<const char*> pointers;
    pointers.reserve(utf8.size());
    for (const auto& item : utf8) {
        pointers.push_back(item.c_str());
    }
    NativeWindowAccess::OnDrop(m_window, static_cast<int>(pointers.size()), pointers.data());
}

void NativeWindow::DispatchDrop(std::vector<std::filesystem::path> paths) {
    if (m_dropHandler) {
        m_dropHandler(std::move(paths));
    }
}

void NativeWindow::DispatchResize(glm::ivec2 size) {
    if (m_resizeHandler) {
        m_resizeHandler(size);
    }
}

Result<VkSurfaceKHR> NativeWindow::CreateSurface(VkInstance instance) {
    using Out = Result<VkSurfaceKHR>;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult result = glfwCreateWindowSurface(instance, m_window, nullptr, &surface);
    if (result != VK_SUCCESS) {
        return Out::Fail({"shell.surface", "glfwCreateWindowSurface failed (VkResult " + std::to_string(result) + ")",
                          static_cast<int32_t>(result)});
    }
    return Out::Ok(surface);
}

std::vector<const char*> NativeWindow::RequiredInstanceExtensions() const {
    uint32_t count = 0;
    const char** names = glfwGetRequiredInstanceExtensions(&count);
    std::vector<const char*> out;
    for (uint32_t i = 0; names != nullptr && i < count; ++i) {
        out.push_back(names[i]);
    }
    return out;
}

}
