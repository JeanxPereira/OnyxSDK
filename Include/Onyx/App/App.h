#pragma once
#include "imgui.h"
#include <Onyx/App/PanelRegistry.h>

#include <Onyx/App/WindowDecorator.h>
#include <Onyx/Viewers/DocumentWindow.h>
#include <Onyx/App/ViewerRegistry.h>
#include <Onyx/Services/AssetDatabase.h>
#include <Onyx/Services/AppConfig.h>
#include <Onyx/Services/RecentFiles.h>
#include <Onyx/Modules/Workspace.h>
#include <GLFW/glfw3.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace Onyx::App {

class App {
public:
    App();
    // `workspace` is owned by Window (constructed before App::init() runs,
    // torn down after App -- see Window.h's member-order comment); App only
    // ever forwards to it, it never owns it.
    void init(GLFWwindow* window, Onyx::Services::AppConfig* config,
              Onyx::Modules::Workspace* workspace);

    // Hook the executable uses to register game-specific panels and viewers.
    // Set before init(); invoked once during init, after the engine's generic
    // panels are registered. Keeps App game-agnostic -- the engine ships no
    // game wiring of its own.
    using AppRegistrar = std::function<void(App&)>;
    void SetRegistrar(AppRegistrar r) { m_registrar = std::move(r); }

    // App-provided default dock layout, invoked once when there is no saved
    // layout (fresh imgui.ini) -- the engine ships no game-specific layout.
    using LayoutFn = std::function<void(ImGuiID dockspaceId)>;
    void SetDefaultLayout(LayoutFn fn) { m_defaultLayout = std::move(fn); }

    // Minimal accessor the registrar uses to add a (game) panel.
    void addPanel(std::unique_ptr<IPanel> panel) { m_panels.add(std::move(panel)); }

    // Show/hide a registered panel by name; false if no such panel. Lets an app
    // open e.g. the "UI Gallery" from a command-line flag or its own menu.
    bool setPanelVisible(std::string_view name, bool visible);

    // Registers a game module with the Workspace (M3b). Valid pre-init
    // only: the registrar's usual call site is fine (it runs from inside
    // init(), before init() has returned), but a call after init() has
    // completed is refused -- by then panels/menus may already assume a
    // fixed module set -- logged and the module is dropped.
    void AddModule(std::unique_ptr<Modules::IGameModule> module);

    // The Workspace this App forwards to; owned by Window, not by App.
    Modules::Workspace& GetWorkspace();

    // Frame phases — called by Window
    void frameBegin();
    void frame();
    void frameEnd();

    bool wantClose() const { return m_wantClose; }

    // UI Component Getters (for external access)
    Onyx::Services::AssetDatabase& getDatabase() { return m_db; }
    Onyx::Services::AppConfig*     getConfig() { return m_config; }
    Onyx::Viewers::DocumentWindow& getDocumentWindow() { return m_documentWindow; }
    ViewerRegistry& getViewerRegistry() { return m_viewerRegistry; }

private:
    void drawMenuBar();
    void drawMenuItems();
    void drawPopups();
    void handleOpenFileRequest();
    void registerPanels();
    void openRecentFile(Onyx::Services::RecentEntry entry);
    std::string getRecentsPath() const;

    Onyx::Services::AssetDatabase   m_db;
    PanelRegistry         m_panels;
    Onyx::Viewers::DocumentWindow   m_documentWindow;
    ViewerRegistry                  m_viewerRegistry;
    WindowDecorator       m_decorator;
    Onyx::Services::AppConfig*      m_config  = nullptr;
    GLFWwindow*           m_window  = nullptr;
    bool                  m_wantClose         = false;

    // Non-owning: set at the top of init(), before registerPanels() runs
    // (so the registrar's AddModule/GetWorkspace calls always see it).
    Onyx::Modules::Workspace* m_workspace = nullptr;
    // Flips true at the end of init(); gates AddModule (pre-init only).
    bool                   m_initDone = false;

    // Injected game-specific panel/viewer registrar (supplied by the app).
    AppRegistrar          m_registrar;

    // Per-app default layout callback.
    LayoutFn              m_defaultLayout;
    bool                  m_resetLayout = false;

    std::string           m_lastAppliedTitle;   // last title pushed to glfwSetWindowTitle

    // Recents
    Onyx::Services::RecentFiles     m_recentFiles;

    // Open file request state
    bool m_requestOpenFile = false;
};

} // namespace Onyx::App
