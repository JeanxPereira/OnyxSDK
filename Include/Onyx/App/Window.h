#pragma once

#include <GLFW/glfw3.h>
#include <vector>
#include <cstdint>
#include <Onyx/App/App.h>
#include <Onyx/Services/AppConfig.h>
#include <Onyx/Services/SessionLog.h>

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
    App                         m_app;
    Onyx::Services::AppConfig   m_config;

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
