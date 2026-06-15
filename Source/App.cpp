#define IMGUI_DEFINE_MATH_OPERATORS
#include <Onyx/App/App.h>
#include <Onyx/App/UIHelpers.h>
#include "imgui_internal.h" // DockBuilder

#include <Onyx/App/IPanel.h>
#include "Ui/NativeMenuBar.h"
#include "Ui/NativeWindow.h"
#include "Ui/TitleBar.h"

#include "Ui/SettingsWindow.h"
#include "Ui/StatusBar.h"

// Viewer headers
#include <Onyx/Viewers/ImageViewer.h>
#include <Onyx/Viewers/Viewport3D.h>

// Core subsystems
#include <Onyx/Services/Events.h>
#include <Onyx/Services/TaskManager.h>
#include <Onyx/Services/ProfileManager.h>
#include <Onyx/Api/ToolkitApi.h>

#include <Onyx/Services/Logger.h>
#include <Onyx/Services/PathUtils.h>
#include "Core/FontManager.h"
#include <Onyx/Fonts/SFSymbols.h>

namespace Onyx::App {

void App::registerPanels() {
  // ── Minimal core (engine) panels — useful for any consumer ──
  // Other generic panels (Iso/Pak Browser, Camera, Anim Curves, Dopesheet,
  // WAD Stats) are now public + opt-in: apps add the ones they want in their
  // registrar (see Onyx/App/Panels/*.h).
  m_panels.add(std::make_unique<StatusBar>());
  m_panels.add(std::make_unique<SettingsWindow>());

  // ── Game (app) panels/viewers ──────────────────────────────────────────────
  // Injected by the executable so the engine stays game-agnostic. Supplies
  // the game browser, inspector, and any game-specific viewer wiring.
  if (m_registrar)
    m_registrar(*this);

  // Set initial visibility for core panels
  if (auto *settings =
          dynamic_cast<SettingsWindow *>(m_panels.find("Settings")))
    settings->visible = false;
}

App::App() {}

void App::init(GLFWwindow *window, Onyx::Services::AppConfig *config) {
  m_window = window;
  m_config = config;

  // Initialize core subsystems
  Onyx::Api::InitParams params;
  params.db = &m_db;
  params.config = config;
  params.viewers = &m_viewerRegistry;
  params.documents = &m_documentWindow;
  Onyx::Api::Init(params);

  // Initialize centralized font system
  Onyx::Fonts::Init();
  Onyx::Fonts::SetIconFont({
      PathUtils::resolvePath("third_party/fonts/SFSymbols.ttf"),
      0xE000, 0xFA19,
      {0.0f, 3.0f}
  });

  // On macOS: nativeDecorations=true means use traffic lights
  // (borderless=false) On Windows/Linux: always borderless custom titlebar
#if defined(__APPLE__)
  m_decorator.borderless = !config->nativeDecorations;
#else
  m_decorator.borderless = true;
#endif

  // WindowDecorator init â€" icon font is now managed by FontManager
  m_decorator.init(window, nullptr);

  // Initialize panels that need config
  registerPanels();
  if (auto *settings =
          dynamic_cast<SettingsWindow *>(m_panels.find("Settings"))) {
    settings->config = config;
    settings->Init();
  }

  // Load recent files
  m_recentFiles.Load(getRecentsPath());

  // Signal startup complete
  EventStartupFinished::post();
}

// â"€â"€ Frame Phases â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€

void App::frameBegin() {
  // Pass menubar height to native window for NCHITTEST
  if (m_window)
    m_decorator.beginFrame(m_window);
}

void App::frame() {
  // Keep the OS/taskbar title in sync with the app-set config title.
  if (m_window && m_config && m_config->windowTitle != m_lastAppliedTitle) {
    glfwSetWindowTitle(m_window, m_config->windowTitle.c_str());
    m_lastAppliedTitle = m_config->windowTitle;
  }

  // Process task manager deferred calls and garbage collection
  Onyx::Services::TaskManager::runDeferredCalls();
  Onyx::Services::TaskManager::collectGarbage();


  // Per-frame tick event for animations, progress bars, etc.
  EventFrameTick::post();

  // â"€â"€ Host window fullscreen â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
  ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::SetNextWindowViewport(vp->ID);

  ImGuiWindowFlags host_flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
      ImGuiWindowFlags_MenuBar;

  ImVec2 framePadding = ImGui::GetStyle().FramePadding;
#if defined(GOWTOOL_OS_MACOS)
  if (!NativeWindow::macosIsFullScreen(m_window) && m_decorator.borderless) {
    framePadding.y = 8.0f; // 1:1 ImHex: 8_scaled for macOS titlebar
  }
#endif

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, framePadding);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::Begin("##HostWindow", nullptr, host_flags);
  ImGui::PopStyleVar(2);

  // â"€â"€ Global Keyboard Shortcuts â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
  ImGuiIO &io = ImGui::GetIO();
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
    m_requestOpenFile = true;
  }
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
    m_documentWindow.CloseActiveTab();
  }

  // Decorator: beginFrame needs to happen after Begin("##HostWindow")
  // so it can read the menu bar height
  frameBegin();

  drawMenuBar();

  ImGui::PopStyleVar(); // Pop FramePadding

  // -- DockSpace ---------------------------------------------------------------
  ImGuiID dockspace_id = ImGui::GetID("GoWToolDockSpace");

  if (m_resetLayout) {
    ImGui::DockBuilderRemoveNode(dockspace_id);
    m_resetLayout = false;
  }
  if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr && m_defaultLayout) {
    m_defaultLayout(dockspace_id);
  }

  ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);
  ImGui::End();

  // â"€â"€ Draw all registered panels â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
  // â"€â"€ Draw all registered panels â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
  m_panels.DrawAll();

  // â"€â"€ Document Window (tab host) â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
  m_documentWindow.Draw();

  // â"€â"€ All popups / modals â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
  drawPopups();
}

void App::frameEnd() {
  // Font rebuild is now handled by Window::frameEnd() via Onyx::Fonts
  // Audio-volume <-> config sync is wired by the app (see AppRegistration).

  // Post-draw tick â€" runs AFTER frame()'s panels/document draw for this frame,
  // matching the old frameEnd() timing. Subscribers (e.g. the audio-volume
  // write-back) can observe same-frame UI mutations here.
  EventFrameEnd::post();
}


void App::handleOpenFileRequest() {
  if (!m_requestOpenFile) return;
  m_requestOpenFile = false;
  if (m_db.IsLoading()) return;

  std::vector<Onyx::Domain::OpenFilter> filters;
  for (auto& p : Onyx::Services::ProfileManager::Get().GetProfiles()) {
    auto f = p->GetOpenFilter();
    if (f.valid()) filters.push_back(std::move(f));
  }

  std::string path = SystemOpenFileDialog(filters);
  if (path.empty()) return;

  // Detect the profile up front (cheap) so we can surface an "unsupported"
  // message and label the recent entry, then load asynchronously — large ISOs
  // must not block the UI thread.
  auto profile = Onyx::Services::ProfileManager::Get().DetectProfileForFile(path);
  if (!profile) {
    LOG_WARN("[App] Unsupported or unreadable file: %s", path.c_str());
    return;
  }
  m_recentFiles.Add(path, profile->GetName(), "");
  m_db.LoadWadAsync(path);
}

void App::openRecentFile(Onyx::Services::RecentEntry entry) {
  if (!fs::exists(entry.path))
    return;

  if (m_db.LoadWad(entry.path))
    m_recentFiles.Add(entry.path, entry.gameHint, entry.fileType);
}

std::string App::getRecentsPath() const {
  return PathUtils::resolvePath("gowtool_recents.txt");
}

void App::drawPopups() { handleOpenFileRequest(); }

void App::drawMenuBar() {
  if (!ImGui::BeginMenuBar())
    return;

  // â"€â"€ Phase 1: Backdrop first so menu items render on top of it â"€â"€â"€â"€â"€â"€â"€â"€â"€
  if (m_window)
    TitleBar::drawBackDrop();

  // â"€â"€ Phase 2: Menu items â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
  // On macOS with native decorations, menus go to the system menu bar.
  // On Windows/Linux (or macOS borderless), menus render in ImGui.
  // NativeMenuBar::enable() persists across frames â€" only call it to
  // set state, never toggle on/off per frame (which would clear NSMenu).

#if defined(GOWTOOL_OS_MACOS)
  bool useNativeMenu = m_config && m_config->nativeMenuBar;
  NativeMenuBar::enable(useNativeMenu);
  if (useNativeMenu)
    NativeMenuBar::beginMainMenuBar();
#endif

  drawMenuItems();

#if defined(GOWTOOL_OS_MACOS)
  if (useNativeMenu)
    NativeMenuBar::endMainMenuBar();
#endif

  // â"€â"€ Phase 3: Titlebar buttons + centered title â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
  if (m_window)
    m_wantClose =
        TitleBar::draw(m_window, m_config ? m_config->windowTitle.c_str() : "Onyx Toolkit", m_decorator.borderless);

  // â"€â"€ Phase 4: macOS borderless drag â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
#if defined(GOWTOOL_OS_MACOS)
  if (m_decorator.borderless) {
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const float menuBarH = ImGui::GetCurrentWindowRead()->MenuBarHeight;
    const ImVec2 menuUnderlaySize(windowSize.x, menuBarH);

    ImGui::SetCursorPos(ImVec2());

    if (!ImGui::IsAnyItemHovered()) {
      const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
      if (ImGui::IsMouseHoveringRect(cursorPos, cursorPos + menuUnderlaySize) &&
          ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        NativeWindow::macosHandleTitlebarDoubleClick(m_window);
      }
      NativeWindow::macosSetWindowMovable(m_window, true);
    } else {
      NativeWindow::macosSetWindowMovable(m_window, false);
    }
  }
#endif

  ImGui::EndMenuBar();
}

void App::drawMenuItems() {
  // When NativeMenuBar is enabled (macOS native), these calls populate NSMenu.
  // When disabled, they call ImGui::BeginMenu/MenuItem directly.

#if defined(GOWTOOL_OS_MACOS)
  // Push menu items right of the traffic lights when rendering in ImGui
  if (!NativeMenuBar::isEnabled() && !NativeWindow::macosIsFullScreen(m_window))
    ImGui::SetCursorPosX(80.0f);
#endif

  if (NativeMenuBar::beginMenu("File")) {
    if (NativeMenuBar::menuItem("Open...", "Ctrl+O"))
      m_requestOpenFile = true;
    if (NativeMenuBar::menuItem("Close All"))
      m_db.CloseAll();

    NativeMenuBar::separator();

    // â"€â"€ Recents submenu â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
    if (NativeMenuBar::beginMenu("Recent Files", !m_recentFiles.Empty())) {
      Onyx::Services::RecentEntry entryToOpen;
      bool shouldOpen = false;

      for (const auto &entry : m_recentFiles.Entries()) {
        // Build label: "filename.iso  [GOW2 Â· ISO]"
        std::string gameLabel;
        if (entry.gameHint == "gow1")
          gameLabel = "GOW1";
        else if (entry.gameHint == "gow2")
          gameLabel = "GOW2";
        else if (entry.gameHint == "ragnarok")
          gameLabel = "GOWR";
        else
          gameLabel = entry.gameHint;

        // std::string label = entry.displayName + "  [" + gameLabel + " " +
        //                     ICON_SF_CUBE_FILL + " " + entry.fileType + "]";
        std::string label = entry.displayName + " [" + gameLabel + "]";

        if (NativeMenuBar::menuItem(label.c_str())) {
          entryToOpen = entry;
          shouldOpen = true;
        }
      }

      NativeMenuBar::separator();
      if (NativeMenuBar::menuItem("Clear Recents")) {
        m_recentFiles.Clear();
        m_recentFiles.Save(getRecentsPath());
      }
      NativeMenuBar::endMenu();

      if (shouldOpen) {
        openRecentFile(entryToOpen);
      }
    }

    NativeMenuBar::separator();
    if (NativeMenuBar::menuItem("Exit", "Alt+F4"))
      exit(0);
    NativeMenuBar::endMenu();
  }

  if (NativeMenuBar::beginMenu("Export")) {
    bool has = Onyx::Api::GetSelected() != nullptr;
    if (NativeMenuBar::menuItem("Export glTF...", "Ctrl+E", false, has)) {
    }
    if (NativeMenuBar::menuItem("Export DDS...", nullptr, false, has)) {
    }
    if (NativeMenuBar::menuItem("Copy Hash", "Ctrl+C", false, has))
      if (Onyx::Api::GetSelected())
        ImGui::SetClipboardText(HashHex(Onyx::Api::GetSelected()->hash).c_str());
    NativeMenuBar::endMenu();
  }

  if (NativeMenuBar::beginMenu("Options")) {
    if (NativeMenuBar::menuItem("Settings...", "Ctrl+,")) {
      if (auto *settings =
              dynamic_cast<SettingsWindow *>(m_panels.find("Settings")))
        settings->visible = true;
    }
    NativeMenuBar::endMenu();
  }

  if (NativeMenuBar::beginMenu("View")) {
    for (auto &p : m_panels) {
      NativeMenuBar::menuItemToggle(std::string(p->getName()).c_str(), nullptr,
                                    &p->visible);
    }
    if (NativeMenuBar::menuItem("Reset Layout"))
      m_resetLayout = true;   // triggers DockBuilderRemoveNode + default-layout rebuild next frame
    NativeMenuBar::endMenu();
  }
}

} // namespace Onyx::App
