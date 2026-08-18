#pragma once

#include <GLFW/glfw3.h>
#include <vector>
#include <cstdint>
#include <Onyx/App/App.h>
#include <Onyx/Services/AppConfig.h>
#include <Onyx/Services/SessionLog.h>
#include <Onyx/Modules/Workspace.h>
#include <Onyx/Services/EventBus.h>
#include <Onyx/Types/TypeCatalog.h>

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

private:
    // ── Platform-specific (implemented per OS in window/platform/) ──
    void configureGLFW();
    void setupNativeWindow();
    void beginNativeWindowFrame();
    void endNativeWindowFrame();

    // ── Lifecycle ────────────────────────────────────────────────────
    void initGLFW();
    void initImGui();
    void exitGLFW();
    void exitImGui();

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
    // joins its JobQueue). m_onDocumentOpened/m_onTreeReady are declared
    // last of the four so they are destroyed before all of them:
    // unsubscribing from m_workspace's EventBus before the bus itself dies
    // is what the Subscription contract wants, and their handlers only
    // touch the SessionLog, so nothing else orders them.
    Onyx::Modules::Workspace     m_workspace{Onyx::Types::TypeCatalog::Get()};
    App                          m_app;
    Onyx::Services::Subscription m_onDocumentOpened;
    Onyx::Services::Subscription m_onTreeReady;

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
