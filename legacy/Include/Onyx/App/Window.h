#pragma once

#include <GLFW/glfw3.h>
#include <vector>
#include <cstdint>
#include <memory>
#include <Onyx/App/App.h>
#include <Onyx/Services/AppConfig.h>
#include <Onyx/Services/SessionLog.h>
#include <Onyx/Modules/Workspace.h>
#include <Onyx/Services/EventBus.h>
#include <Onyx/Types/TypeCatalog.h>

// Forward-declared only (never included here): Window owns the app's single
// VkContext/RenderContext (T9) behind unique_ptr<incomplete-type> so that
// consumers of this widely-included header don't also have to see volk.h/
// vk_mem_alloc.h (and, more subtly, so this header's own `#include
// <GLFW/glfw3.h>` above never has to race a Vulkan header for include
// order -- see VkContext.h's binding include-order rule, and Window.cpp's
// own top-of-file comment for how it resolves that race for itself).
// ~Window() is defined out-of-line in Window.cpp, where both types are
// complete, which is what makes unique_ptr<incomplete-type> members legal
// here.
namespace Onyx::Rendering {
class VkContext;
class RenderContext;
} // namespace Onyx::Rendering

namespace Onyx::App {

class Window {
public:
    Window();
    ~Window();

    // Finalizes App init and enters the frame loop. App init is deferred to
    // here (not the constructor) so the executable can inject the game
    // panel/viewer registrar via app() before App::init() runs.
    void run();

    void loop();
    void fullFrame();

    // Called from main() before constructing Window
    static void initNative();

    GLFWwindow* getGLFWwindow() const { return m_window; }

    // Minimal accessor so the executable can install its game-specific
    // registrar onto the App before run()/init().
    App& app() { return m_app; }

    // The Workspace this Window owns (M3b). App::AddModule/GetWorkspace
    // forward to this same instance (see App::init()) -- this accessor
    // exists for callers that reach the Window directly.
    Onyx::Modules::Workspace& workspace() { return m_workspace; }

    // Config accessors
    bool isBorderless() const { return !m_config.nativeDecorations; }

    // The raw floor (T8/T9): the Shell's own VkContext (presentSupport =
    // true) and the RenderContext every frame Executes between the scene
    // draw and the ImGui pass (see frameEnd()'s doc comment in Window.cpp).
    // Callers that want to record their own Vulkan commands directly onto
    // the swapchain image -- a future viewer, a consuming app -- reach it
    // through here rather than each owning a second VkContext. Valid from
    // after Window's constructor returns until ~Window() begins tearing
    // down (i.e. for the same lifetime run()'s caller already assumes for
    // getGLFWwindow()).
    Onyx::Rendering::VkContext&     vkContext()     { return *m_vkContext; }
    Onyx::Rendering::RenderContext& renderContext() { return *m_renderContext; }

private:
    // ── Platform-specific (implemented per OS in window/platform/) ──
    void configureGLFW();
    void setupNativeWindow();
    void beginNativeWindowFrame();
    void endNativeWindowFrame();

    // ── Lifecycle ────────────────────────────────────────────────────
    void initGLFW();
    void initVulkan();   // VkContext + surface + swapchain + frame sync (T9)
    void initImGui();
    void exitGLFW();
    void exitVulkan();
    void exitImGui();

    // ── Swapchain (T9) ───────────────────────────────────────────────
    // Implemented in Window.cpp against the opaque VulkanState below;
    // recreateSwapchain() is what resize/minimize-restore calls into
    // (frameEnd()'s present step also calls it on VK_ERROR_OUT_OF_DATE_KHR/
    // VK_SUBOPTIMAL_KHR). Neither touches the per-frame-in-flight sync
    // objects (semaphores/fences/command pools) createFrameSync() owns --
    // those are sized to kFramesInFlight, not to the swapchain's own image
    // count, so a resize never needs to recreate them.
    void createSwapchain(uint32_t width, uint32_t height);
    void destroySwapchain();
    void recreateSwapchain();
    void createFrameSync();
    void destroyFrameSync();
    void presentFrame(); // acquire/record/submit/present one swapchain image; frameEnd()'s helper

    // ── Frame phases ─────────────────────────────────────────────────
    void frameBegin();
    void frame();
    void frameEnd();

    // ── Optimizations ────────────────────────────────────────────────
    // Frame pacing lives in Onyx::Frame now: clients that animate say so, rather
    // than the window guessing from vertex-buffer diffs and input state.

    // ── Members ──────────────────────────────────────────────────────
    GLFWwindow*  m_window   = nullptr;
    Onyx::Services::AppConfig   m_config;

    // Vulkan (T9). unique_ptr<incomplete-type> per the file comment at top
    // (VkContext/RenderContext are only forward-declared here; VulkanState
    // -- the swapchain, per-image views/layouts/semaphores, and the
    // frames-in-flight command pools/semaphores/fences -- is a nested type
    // defined entirely inside Window.cpp, never named outside it). All
    // three are explicitly reset() by exitVulkan(), called from ~Window()'s
    // body before this section's implicit member destructors ever run --
    // so, unlike m_workspace/m_app below, their declaration position here
    // carries no ordering contract of its own: by the time the implicit
    // destructors reach them they are already null.
    std::unique_ptr<Onyx::Rendering::VkContext>     m_vkContext;
    std::unique_ptr<Onyx::Rendering::RenderContext> m_renderContext;
    struct VulkanState;
    std::unique_ptr<VulkanState> m_vk;

    // ── Member order below is the construction/destruction-order contract
    // (mirrors the comment on Modules::Workspace's own members) ───────────
    // Members construct in declaration order and destroy in reverse.
    // m_workspace is declared AFTER m_config so config is already loaded
    // (see the constructor body) by the time anything reachable through the
    // Workspace could read it. m_app is declared AFTER m_workspace so App
    // -- which holds the PanelRegistry and, from this task on, a raw
    // pointer back into m_workspace (see App::init()) -- is destroyed
    // FIRST: panels must be gone before the Workspace they may reach
    // through App::GetWorkspace() is torn down (and before ~Workspace()
    // joins its JobQueue). m_onDocumentOpened/m_onTreeReady/
    // m_onDocumentClosed are declared last of the five so they are
    // destroyed before all of them: unsubscribing from m_workspace's
    // EventBus before the bus itself dies is what the Subscription
    // contract wants. m_onDocumentOpened/m_onTreeReady's handlers only
    // touch the SessionLog, so nothing else orders them;
    // m_onDocumentClosed (Task 13a) also reaches into m_app (via
    // getDocumentWindow()), which is exactly why it must be declared
    // after m_app too.
    Onyx::Modules::Workspace     m_workspace{Onyx::Types::TypeCatalog::Get()};
    App                          m_app;
    Onyx::Services::Subscription m_onDocumentOpened;
    Onyx::Services::Subscription m_onTreeReady;
    Onyx::Services::Subscription m_onDocumentClosed;

    // Frame rate control
    double m_fpsUnlockedEndTime    = 0.0;
    bool   m_firstFrame            = true;

    // Vtx buffer diff

    // Config path
    std::string m_configPath;

    // Backing storage for io.IniFilename — must outlive the ImGui context.
    std::string m_imguiIniPath;

    // This run's log file, opened before anything else so the whole boot
    // lands in it. Detached last, on the way out.
    Onyx::Services::SessionLog::Session m_sessionLog;
};

} // namespace Onyx::App
