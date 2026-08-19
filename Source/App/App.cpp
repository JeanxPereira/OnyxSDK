#define IMGUI_DEFINE_MATH_OPERATORS
#include <Onyx/App/App.h>
#include <Onyx/App/UIHelpers.h>
#include "imgui_internal.h" // DockBuilder

#include <Onyx/App/IPanel.h>
#include "App/NativeMenuBar.h"
#include "App/NativeWindow.h"
#include "App/TitleBar.h"

#include "App/SettingsWindow.h"
#include "App/StatusBar.h"
#include <Onyx/App/Panels/DocumentBrowser.h>
#include <Onyx/App/Panels/InspectorPanel.h>
#include <Onyx/App/Panels/UiGallery.h>

// Viewer headers
#include <Onyx/Viewers/ImageViewer.h>
#include <Onyx/Viewers/Viewport3D.h>

// Core subsystems
#include <Onyx/Services/Events.h>
#include <Onyx/Services/TaskManager.h>
#include <Onyx/Api/ToolkitApi.h>

#include <Onyx/Services/Logger.h>
#include <Onyx/Services/PathUtils.h>
#include "Fonts/FontManager.h"
#include <Onyx/Services/Appearance.h>
#include <Onyx/Fonts/SFSymbols.h>

namespace Onyx::App {

void App::registerPanels() {
  // ── Minimal core (engine) panels — useful for any consumer ──
  // Other generic panels (Iso/Pak Browser, Camera, Anim Curves, Dopesheet,
  // WAD Stats) are now public + opt-in: apps add the ones they want in their
  // registrar (see Onyx/App/Panels/*.h).
  // StatusBar now reads the Workspace directly (M3b Task 5), same as
  // DocumentBrowser below -- m_workspace is set at the top of init(),
  // before this runs, so it is only ever null if some future call site
  // invokes registerPanels() before that assignment; guarded defensively.
  if (m_workspace)
    m_panels.add(std::make_unique<StatusBar>(*m_workspace));
  m_panels.add(std::make_unique<SettingsWindow>());
  // UI test mode. Hidden by default (the panel's ctor clears `visible`); the
  // View menu picks it up automatically because it iterates the registry.
  m_panels.add(std::make_unique<UiGallery>());
  // Generic Workspace document tree (M3b).
  if (m_workspace)
    m_panels.add(std::make_unique<DocumentBrowser>(*m_workspace));
  // Generic Workspace inspector -- hosts InfoTab's Draw() (M3b Task 6; the
  // profile-era AssetDatabase leg InfoTab used to also support is gone).
  if (m_workspace)
    m_panels.add(std::make_unique<InspectorPanel>(*m_workspace));

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

void App::init(GLFWwindow *window, Onyx::Services::AppConfig *config,
                Onyx::Modules::Workspace *workspace) {
  m_window = window;
  m_config = config;
  // Set first: registerPanels() below runs the app's registrar, which is
  // the usual place a consumer calls AddModule()/GetWorkspace().
  m_workspace = workspace;

  // Initialize core subsystems
  Onyx::Api::InitParams params;
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

  // The font list exists now, so the environment can say what an unset font
  // path resolves to. Keeping this out of State is what lets the "user has no
  // choice saved" case be a real empty value instead of a sentinel index.
  {
    Onyx::Appearance::Environment env = Onyx::Appearance::Env();
    const auto &fonts = Onyx::Fonts::GetFontList();
    const int def = Onyx::Fonts::DefaultFontIndex();
    if (def >= 0 && def < (int)fonts.size())
      env.defaultFontPath = fonts[def].path;
    Onyx::Appearance::SetEnvironment(env);
  }

  // On macOS: nativeDecorations=true means use traffic lights
  // (borderless=false) On Windows/Linux: always borderless custom titlebar
#if defined(__APPLE__)
  m_decorator.borderless = !config->nativeDecorations;
#else
  m_decorator.borderless = true;
#endif

  // WindowDecorator init — icon font is now managed by FontManager
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

  // AddModule is refused from here on (pre-init only).
  m_initDone = true;
}

void App::AddModule(std::unique_ptr<Modules::IGameModule> module) {
  if (m_initDone) {
    LOG_ERR("[App] AddModule called after init(); module dropped (pre-init only)");
    return;
  }
  if (!m_workspace) {
    LOG_ERR("[App] AddModule called before the Workspace exists; module dropped");
    return;
  }
  m_workspace->AddModule(std::move(module));
}

Modules::Workspace &App::GetWorkspace() { return *m_workspace; }

// ── Frame Phases ────────────────────────────────────────────────────────────

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

  // ── Host window fullscreen ─────────────────────────────────────────────
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
#if defined(ONYX_OS_MACOS)
  if (!NativeWindow::macosIsFullScreen(m_window) && m_decorator.borderless) {
    framePadding.y = 8.0f; // 1:1 ImHex: 8_scaled for macOS titlebar
  }
#endif

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, framePadding);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::Begin("##HostWindow", nullptr, host_flags);
  ImGui::PopStyleVar(2);

  // ── Global Keyboard Shortcuts ──────────────────────────────────────────
  ImGuiIO &io = ImGui::GetIO();
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
    m_requestOpenFile = true;
  }
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
    m_documentWindow.CloseActiveTab();
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
    if (IPanel *gallery = m_panels.find("UI Gallery"))
      gallery->visible = !gallery->visible;
  }

  // Decorator: beginFrame needs to happen after Begin("##HostWindow")
  // so it can read the menu bar height
  frameBegin();

  drawMenuBar();

  ImGui::PopStyleVar(); // Pop FramePadding

  // -- DockSpace ---------------------------------------------------------------
  ImGuiID dockspace_id = ImGui::GetID("OnyxDockSpace");

  if (m_resetLayout) {
    ImGui::DockBuilderRemoveNode(dockspace_id);
    m_resetLayout = false;
  }
  if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr && m_defaultLayout) {
    m_defaultLayout(dockspace_id);
  }

  ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);
  ImGui::End();

  // ── Draw all registered panels ─────────────────────────────────────────
  // ── Draw all registered panels ─────────────────────────────────────────
  m_panels.DrawAll();

  // ── Document Window (tab host) ─────────────────────────────────────────
  m_documentWindow.Draw();

  // ── All popups / modals ────────────────────────────────────────────────
  drawPopups();
}

void App::frameEnd() {
  // Font rebuild is now handled by Window::frameEnd() via Onyx::Fonts
  // Audio-volume <-> config sync is wired by the app (see AppRegistration).

  // Post-draw tick — runs AFTER frame()'s panels/document draw for this frame,
  // matching the old frameEnd() timing. Subscribers (e.g. the audio-volume
  // write-back) can observe same-frame UI mutations here.
  EventFrameEnd::post();
}


void App::handleOpenFileRequest() {
  if (!m_requestOpenFile) return;
  m_requestOpenFile = false;
  if (!m_workspace) return;

  std::vector<Onyx::Modules::OpenFilter> filters;
  for (auto& module : m_workspace->Modules())
    for (auto& f : module->Info().openFilters)
      if (!f.extensions.empty()) filters.push_back(f);

  std::string path = SystemOpenFileDialog(filters);
  if (path.empty()) return;

  // Workspace-only (M3b Task 6): the legacy AssetDatabase fallback that used
  // to run when no module claimed the file is gone. No winner means no
  // module in this process can open it -- log and stop, nothing to crash.
  auto rank = m_workspace->Probe(path);
  if (!rank.winner) {
    LOG_WARN("[App] no module accepts %s", path.c_str());
    return;
  }

  m_recentFiles.Add(path, rank.winner->Info().id, "");
  m_workspace->OpenAsync(path);
}

void App::openRecentFile(Onyx::Services::RecentEntry entry) {
  if (!fs::exists(entry.path))
    return;
  if (!m_workspace)
    return;

  // Re-probe rather than trust the recorded gameHint verbatim: OpenAsync
  // treats an explicit hint that fails to resolve as final (no probe
  // fallback -- see Workspace::Open's doc comment), so a stale/renamed hint
  // would silently open nothing instead of falling back the way this used
  // to via ProfileManager::FindProfileByHint-then-DetectProfileForFile.
  auto rank = m_workspace->Probe(entry.path);
  if (!rank.winner) {
    LOG_WARN("[App] no module accepts %s", entry.path.c_str());
    return;
  }

  m_workspace->OpenAsync(entry.path);
  m_recentFiles.Add(entry.path, entry.gameHint, entry.fileType);
}

std::string App::getRecentsPath() const {
  return PathUtils::resolvePath("onyx_recents.txt");
}

void App::drawPopups() { handleOpenFileRequest(); }

bool App::setPanelVisible(std::string_view name, bool visible) {
  IPanel *panel = m_panels.find(name);
  if (!panel)
    return false;
  panel->visible = visible;
  return true;
}

void App::drawMenuBar() {
  if (!ImGui::BeginMenuBar())
    return;

  // ── Phase 1: Backdrop first so menu items render on top of it ─────────
  if (m_window)
    TitleBar::drawBackDrop();

  // ── Phase 2: Menu items ─────────────────────────────────────────────
  // On macOS with native decorations, menus go to the system menu bar.
  // On Windows/Linux (or macOS borderless), menus render in ImGui.
  // NativeMenuBar::enable() persists across frames — only call it to
  // set state, never toggle on/off per frame (which would clear NSMenu).

#if defined(ONYX_OS_MACOS)
  bool useNativeMenu = m_config && m_config->nativeMenuBar;
  NativeMenuBar::enable(useNativeMenu);
  if (useNativeMenu)
    NativeMenuBar::beginMainMenuBar();
#endif

  drawMenuItems();

#if defined(ONYX_OS_MACOS)
  if (useNativeMenu)
    NativeMenuBar::endMainMenuBar();
#endif

  // ── Phase 3: Titlebar buttons + centered title ──────────────────────
  if (m_window)
    m_wantClose =
        TitleBar::draw(m_window, m_config ? m_config->windowTitle.c_str() : "Onyx Toolkit", m_decorator.borderless);

  // ── Phase 4: macOS borderless drag ──────────────────────────────────
#if defined(ONYX_OS_MACOS)
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

#if defined(ONYX_OS_MACOS)
  // Push menu items right of the traffic lights when rendering in ImGui
  if (!NativeMenuBar::isEnabled() && !NativeWindow::macosIsFullScreen(m_window))
    ImGui::SetCursorPosX(80.0f);
#endif

  if (NativeMenuBar::beginMenu("File")) {
    if (NativeMenuBar::menuItem("Open...", "Ctrl+O"))
      m_requestOpenFile = true;
    if (NativeMenuBar::menuItem("Close All")) {
      // AssetDatabase::CloseAll() is gone (M3b Task 6) -- close every
      // Workspace document (copy the ids first: Close() erases from the
      // vector Documents() views) and every open viewer tab, matching what
      // EventAllClosed used to trigger on DocumentWindow.
      if (m_workspace) {
        std::vector<Onyx::Modules::DocumentId> ids;
        ids.reserve(m_workspace->Documents().size());
        for (auto &doc : m_workspace->Documents())
          ids.push_back(doc->id);
        for (auto id : ids)
          m_workspace->Close(id);
      }
      m_documentWindow.CloseAll();
    }

    NativeMenuBar::separator();

    // ── Recents submenu ─────────────────────────────────────────────
    if (NativeMenuBar::beginMenu("Recent Files", !m_recentFiles.Empty())) {
      Onyx::Services::RecentEntry entryToOpen;
      bool shouldOpen = false;

      for (const auto &entry : m_recentFiles.Entries()) {
        // Build label: "filename.iso  [GOW2 · ISO]"
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
    // These items used to gate on Onyx::Api::GetSelected(), the profile-era
    // raw-pointer selection ToolkitApi exposed alongside EventAssetSelected
    // (both removed in M3b Task 6). The Workspace's own selection model
    // (Modules::SelectionChanged, consumed by InfoTab/InspectorPanel) has
    // no equivalent global accessor at this layer yet, so all three items
    // stay disabled until that wiring lands -- none had a working export
    // body regardless (Export glTF/DDS were always empty stubs).
    bool has = false;
    if (NativeMenuBar::menuItem("Export glTF...", "Ctrl+E", false, has)) {
    }
    if (NativeMenuBar::menuItem("Export DDS...", nullptr, false, has)) {
    }
    if (NativeMenuBar::menuItem("Copy Hash", "Ctrl+C", false, has)) {
    }
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
    // No programmatic default layout exists to rebuild into (m_defaultLayout
    // is never set by any caller -- SetDefaultLayout exists on App but
    // nothing calls it), so this is the honest minimal version: delete the
    // saved dock layout (DockBuilderRemoveNode below recursively removes
    // dockspace_id's whole node tree, including any orphan empty central
    // node an old imgui.ini accumulated) and let ImGui start fresh next
    // frame, same as a first run with no imgui.ini at all.
    if (NativeMenuBar::menuItem("Reset Layout")) {
      m_resetLayout = true;   // triggers DockBuilderRemoveNode + default-layout rebuild next frame
      LOG_INFO("[App] View > Reset Layout: dock layout cleared; ImGui will re-lay out panels "
               "next frame");
    }
    NativeMenuBar::endMenu();
  }
}

} // namespace Onyx::App
