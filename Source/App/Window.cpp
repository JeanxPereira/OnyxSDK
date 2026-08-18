#include <glad/glad.h>  // Must be before GLFW
#include <Onyx/App/Window.h>

#include <cfloat>
#include <cstring>
#include <string>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include <Onyx/Services/PathUtils.h>
#include <Onyx/Services/TaskManager.h>
#include <Onyx/Services/ThemeManager.h>
#include <Onyx/Services/Appearance.h>
#include <Onyx/Services/FrameScheduler.h>
#include "Services/ScaleManager.h"
#include "Fonts/FontManager.h"
#include <Onyx/Services/Events.h>
#include "App/NativeWindow.h"

namespace Onyx::App {

// -- Globals needed by GLFW callbacks -----------------------------------------
static Window* s_windowInstance = nullptr;

static void glfw_error_callback(int error, const char* desc) {
    // GLFW_PLATFORM_ERROR (65544) "Cannot query workarea without screen" is a
    // known benign error that occurs on macOS when running under a remote
    // desktop session (TeamViewer, VNC, RDP, or any virtual display) instead
    // of a real physical NSScreen.  The Cocoa backend queries the monitor's
    // usable area during window creation/positioning; that call fails
    // gracefully on a virtual display -- the window still opens and renders
    // correctly, so the message is just noise.  All other errors are logged.
#if defined(__APPLE__)
    if (error == GLFW_PLATFORM_ERROR && desc && strstr(desc, "workarea"))
        return;
#endif
    fprintf(stderr, "GLFW error %d: %s\n", error, desc);
}

// -- Constructor / Destructor --------------------------------------------------

Window::Window()
    : m_configPath(PathUtils::resolvePath("onyx.toml"))
{
    s_windowInstance = this;

    m_config = Onyx::Services::AppConfig::load(m_configPath);
    Onyx::Services::AppConfig::SetInstance(&m_config);

    initGLFW();
    initImGui();
    setupNativeWindow();

    // Initialize core systems
    Onyx::Services::TaskManager::init();
}

// -- run -- finalize App and enter the frame loop ------------------------------
// App init is deferred out of the constructor so the executable can inject the
// game panel/viewer registrar (via app()) before App::init() -- which is what
// invokes the registrar -- runs.

void Window::run() {
    m_app.init(m_window, &m_config);

    // 1:1 ImHex: live resize via OS refresh callback
    glfwSetWindowRefreshCallback(m_window, [](GLFWwindow*) {
        if (s_windowInstance)
            s_windowInstance->fullFrame();
    });

    glfwSetDropCallback(m_window, [](GLFWwindow*, int, const char**) {});

    loop();
}

Window::~Window() {
    // Save config before shutdown
    {
        bool maximized = glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED);
        m_config.maximized = maximized;
        if (!maximized) {
            glfwGetWindowPos(m_window, &m_config.windowX, &m_config.windowY);
            glfwGetWindowSize(m_window, &m_config.windowW, &m_config.windowH);
        }

        // Panels no longer write appearance fields into the config; the owner
        // does, once, here. One less thing every slider has to remember.
        m_config.setAppearanceState(Onyx::Appearance::Get());

        m_config.save(m_configPath);
    }

    // Shutdown core systems
    EventShutdown::post();
    Onyx::Services::EventManager::clear();
    Onyx::Services::TaskManager::exit();

    exitImGui();
    exitGLFW();

    s_windowInstance = nullptr;
}

// -- initGLFW -----------------------------------------------------------------

void Window::initGLFW() {
    glfwSetErrorCallback(glfw_error_callback);

#if defined(__APPLE__)
    // Prevent the Cocoa backend from trying to install a global application
    // menu bar before a real screen is available.  On virtual/remote displays
    // (TeamViewer, VNC, headless CI) this avoids spurious platform errors
    // during glfwInit() and subsequent monitor queries.
    glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
#endif

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        std::exit(-1);
    }

    // Delegate platform-specific GL hints
    configureGLFW();

    m_window = glfwCreateWindow(m_config.windowW, m_config.windowH,
                                m_config.windowTitle.c_str(), nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        fprintf(stderr, "Failed to create GLFW window\n");
        std::exit(-1);
    }

    glfwSetWindowPos(m_window, m_config.windowX, m_config.windowY);
    if (m_config.maximized)
        glfwMaximizeWindow(m_window);

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    // Track window attributes dynamically since macOS shutdown can miss late bounds queries
    glfwSetWindowPosCallback(m_window, [](GLFWwindow* window, int xpos, int ypos) {
        if (!glfwGetWindowAttrib(window, GLFW_MAXIMIZED)) {
            if (Window* w = (Window*)glfwGetWindowUserPointer(window)) {
                w->m_config.windowX = xpos;
                w->m_config.windowY = ypos;
            }
        }
    });

    glfwSetWindowSizeCallback(m_window, [](GLFWwindow* window, int width, int height) {
        if (!glfwGetWindowAttrib(window, GLFW_MAXIMIZED)) {
            if (Window* w = (Window*)glfwGetWindowUserPointer(window)) {
                w->m_config.windowW = width;
                w->m_config.windowH = height;
            }
        }
    });

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr, "Failed to initialize OpenGL loader\n");
        std::exit(-1);
    }

    // Store 'this' for callbacks
    glfwSetWindowUserPointer(m_window, this);
}

// -- initImGui ----------------------------------------------------------------

void Window::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    m_imguiIniPath = PathUtils::resolvePath("imgui.ini");
    io.IniFilename = m_imguiIniPath.c_str(); // ImGui owns its native ini (auto-loads/saves)
    io.IniSavingRate = 2.0f;

    // Apply centralized accent-derived theme (replaces StyleColorsDark + applyAccent)
    Onyx::Theme::ApplyTheme(m_config.getAccent(),
                           (Onyx::Theme::ThemeMode)m_config.themeMode);


    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::GetStyle().WindowRounding = 0.0f;
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Initialize centralized scale system with OS native DPI
    float nativeScale = 1.0f;
#if defined(__APPLE__)
    {
        float xscale = 1.0f, yscale = 1.0f;
        glfwGetWindowContentScale(m_window, &xscale, &yscale);
        nativeScale = xscale;
    }
#endif
    Onyx::Scale::Init(m_config.uiScale, nativeScale);

    // Seed the appearance owner from config. The font half of the environment
    // is filled in later (App::init populates the font list first); the first
    // Commit() runs at the end of frame 1, by which time everything is ready.
    {
        Onyx::Appearance::Environment env;
        env.nativeScale       = nativeScale;
        env.systemPrefersDark = (Onyx::Theme::GetEffectiveMode() != Onyx::Theme::ThemeMode::Light);
        Onyx::Appearance::SetEnvironment(env);

        Onyx::Appearance::Set(m_config.appearanceState());

        // Apply now so frame 1 already has the house metrics. The font list is
        // not populated yet, so this commit skips the atlas; App::init fills in
        // the default font path and the next commit picks it up.
        Onyx::Appearance::Commit();
    }

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
#if defined(__APPLE__)
    ImGui_ImplOpenGL3_Init("#version 150");
#else
    ImGui_ImplOpenGL3_Init("#version 330");
#endif
}

// -- exitImGui / exitGLFW -----------------------------------------------------

void Window::exitImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}

void Window::exitGLFW() {
    glfwDestroyWindow(m_window);
    glfwTerminate();
    m_window = nullptr;
}

// -- loop ---------------------------------------------------------------------

void Window::loop() {
    while (!glfwWindowShouldClose(m_window)) {
        // Smart event handling: poll aggressively when active, idle otherwise
        if (m_firstFrame) {
            Onyx::Frame::BeginFrame(glfwGetTime());
            glfwPollEvents();
            m_firstFrame = false;
        } else {
            // Time-driven work (colour transitions, progress, playback) has
            // no input to infer activity from, so it asks explicitly.
            const bool animating = Onyx::Frame::BeginFrame(glfwGetTime());
            // Input keeps the loop hot while the user is actually doing
            // something; everything else now comes through the scheduler.
            //
            // Note what is NOT here any more: "any window is a separate OS
            // viewport". That pinned the app at full speed forever the moment a
            // panel was undocked -- measured at ~180 fps with nothing on screen
            // moving. Dragging a floating window is already covered by the
            // mouse tests, and viewports are rendered every frame regardless.
            const bool active = animating
                       || ImGui::IsMouseDown(ImGuiMouseButton_Left)
                       || ImGui::IsMouseDown(ImGuiMouseButton_Right)
                       || ImGui::IsMouseDown(ImGuiMouseButton_Middle)
                       || ImGui::IsAnyItemActive()
                       || ImGui::IsKeyDown(ImGuiMod_Ctrl);

#if defined(__APPLE__)
            if (NativeWindow::macosIsWindowBeingResized(m_window))
                Onyx::Frame::RequestRedraw();
#endif

            if (active) {
                glfwPollEvents();
            } else {
                glfwWaitEventsTimeout(1.0 / 15.0); // ~15 FPS idle (down from 30)
            }
        }

#if defined(__APPLE__)
        // Suppress hover effects during live resize (ImHex pattern)
        if (NativeWindow::macosIsWindowBeingResized(m_window))
            ImGui::GetIO().MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
#endif

        fullFrame();

        if (m_app.wantClose())
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
    }
}

// -- fullFrame -- crash-protected frame ---------------------------------------

void Window::fullFrame() {
    if (!m_window) return;

    static uint32_t crashWatchdog = 0;

#if !defined(DEBUG) && !defined(_DEBUG)
    try {
#endif
        frameBegin();
        frame();
        frameEnd();
#if !defined(DEBUG) && !defined(_DEBUG)
        crashWatchdog = 0;
    } catch (...) {
        if (++crashWatchdog > 10) std::abort();
        ImGui::EndFrame();
        fprintf(stderr, "Exception caught in fullFrame(), watchdog=%u\n", crashWatchdog);
    }
#endif
}

// -- frameBegin ---------------------------------------------------------------

void Window::frameBegin() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Drive smooth color transitions (preset switches with ease-out)
    Onyx::Theme::UpdateTransition();

    beginNativeWindowFrame();
}

// -- frame --------------------------------------------------------------------

void Window::frame() {
    m_app.frame();
}

// -- frameEnd -----------------------------------------------------------------

void Window::frameEnd() {
    endNativeWindowFrame();

    ImGui::Render();

    // Always present. The old vertex-buffer diff could not see an FBO whose
    // pixels changed behind an unchanged texture id, and the event wait already
    // caps the idle rate -- Onyx::Frame decides when that cap lifts.
    {
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData) {
            int w, h;
            glfwGetFramebufferSize(m_window, &w, &h);
            glViewport(0, 0, w, h);
            glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(drawData);
            glfwSwapBuffers(m_window);
        }
    }

    // Viewport windows (external OS windows) are always updated and rendered:
    // dragging one flickers otherwise, since its content changes without the
    // main window's own draw data changing.
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup);
    }

    // Font rebuild MUST happen after all rendering is complete.
    // Rebuilding the atlas before Render() invalidates the font texture
    // that the current frame's draw commands reference.
    // One owner, one place, once per frame, outside the frame: Commit applies
    // any pending appearance change and uploads a rebuilt atlas. A frame with
    // nothing pending costs a single state comparison.
    Onyx::Appearance::Commit();

    if (Onyx::Fonts::IsPendingRebuild()) {
        Onyx::Fonts::UploadAtlas();
    }

    m_app.frameEnd();
}


} // namespace Onyx::App
