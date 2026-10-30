# OnyxSDK survey: application shell and UI layer

Scope: Source/App (+Panels, Platform), Source/Services, Source/Fonts, Source/Platform, Include/Onyx/{App,Services,Platform}.
Method: read-only. NOTE: `hooks/code_tree.py` does NOT exist in the repo, so the symbol map was built with grep/wc and by reading the files.
Base: branch feat/host-ui-hooks, HEAD 6dd2be6. Line numbers are approximate (+-3) where marked "~".

## 0. Size and shape

- Biggest files: Window.cpp 1185, UiGallery.cpp 861, SettingsWindow.cpp 758, ThemeManager.cpp 595, App.cpp 518, TaskManager.cpp 322, Widgets.cpp 313.
- Include/Onyx/App/App.h pulls imgui.h, GLFW, Workspace.h, DocumentWindow.h, AppConfig.h, RecentFiles.h. Every host TU including App.h or Window.h pays for all of it.
- Several big files are "1:1 ImHex" ports (comments say so: Window.cpp:185, SettingsWindow.cpp:74, TitleBar.cpp, NativeWindow_windows.cpp). They carry ImHex's static-heavy structure without ImHex's plugin architecture.

## 1. Ownership and call graph (text)

```
main (host exe)
 `- Onyx::App::Window            Window.cpp  (GOD OBJECT)
     |- owns GLFWwindow + glfw callbacks (267-325); global s_windowInstance (45)
     |- owns VkContext, RenderContext, VulkanState{surface,swapchain,sync} (333-682)
     |- owns ImGui + ImPlot contexts, ImGui_Impl{Glfw,Vulkan} (686-791)
     |- owns present path (presentFrame 1003-1183: raw barriers/submit/present)
     |- owns AppConfig m_config  (+ AppConfig::SetInstance(&m_config) line 136, never cleared)
     |- owns Modules::Workspace m_workspace (EventBus + JobQueue)
     |- owns App m_app
     |- owns SessionLog + 3 event subscriptions (m_onDocumentOpened/TreeReady/Closed)
     |- writes onyx.toml ONLY in ~Window (196-211)
     `- starts/stops TaskManager (174, 216)
     run(): m_app.init(window,&config,&workspace) -> loop()
     loop(): Frame::BeginFrame -> poll or waitTimeout(1/15) -> fullFrame()
     fullFrame(): [release only: try/catch(...)] frameBegin(); frame(); frameEnd()
       frameBegin: ImGui NewFrame, Appearance::Tick
       frame:      Workspace.Jobs().Pump(); Events().Pump(); m_app.frame()
       frameEnd:   ImGui::Render, present, platform windows, Appearance::Commit, Fonts::UploadAtlas, m_app.frameEnd()

App (App.cpp)  (second god object)
 |- PanelRegistry (StatusBar, SettingsWindow, UiGallery, DocumentBrowser, InspectorPanel + host panels)
 |- Viewers::DocumentWindow m_documentWindow (tab host, NOT an IPanel)
 |- ViewerRegistry, WindowDecorator, RecentFiles
 |- hardcoded menu bar (drawMenuBar/drawMenuItems), global shortcuts (Ctrl+O/W/F1), DockBuilder layout
 |- window title sync (frame():159), per-frame EventFrameTick/EventFrameEnd, TaskManager::runDeferredCalls
 |- Api::Init(config, viewers, documents) -> ToolkitApi global facade (App.cpp:80-84)
 |- Fonts::Init + SetIconFont + Appearance::SetEnvironment (86-104)
 `- dynamic_cast<SettingsWindow*>(m_panels.find("Settings")) at App.cpp:64,119,490
```

Circular knowledge:
- Window <-> App: Window's DocumentClosed handler reaches into `m_app.getDocumentWindow()` (Window.cpp:165); `~Window` calls `m_app.getDocumentWindow().Shutdown()` (250) to fix a destruction-order bug "found live". The ordering contract is a 25-line comment in Window.h:110-135.
- Services -> UI: AppConfig maps to/from `Appearance::State` (AppConfig.cpp:16-42). `Appearance.h` includes imgui.h and `ThemeManager.h`; `ThemeManager.cpp` calls `Appearance::Mutate/Get`; `Appearance.cpp` calls `Theme::BuildPalette`. Appearance <-> ThemeManager mutual dependency.
- Appearance -> Fonts (Appearance.cpp:242 BuildAtlas); ScaleManager -> Appearance.
- Rendering/Viewers -> `Services::AppConfig::Get()` global (Viewport3D.cpp:165,379; Camera.cpp:9 comment; CameraPanel.cpp:16,34). The render layer reads a UI-shell config singleton.
- Config reachable via five paths: `Window::m_config`, `AppConfig::Get()`, `Api::Config()`, `App::getConfig()`, `SettingsWindow::config`.

God objects: Window (platform window + Vulkan presentation + ImGui backend + frame loop + persistence + Workspace owner + tab wiring; ~600 of its 1185 lines are Vulkan swapchain code that has no reason to share a class with config saving), App (menu model + shortcuts + layout + recents + panel registry + title sync + events + open-file flow + hooks), SettingsWindow (raw `AppConfig*` poking + its own animation system).

## 2. Host hooks: ad hoc, not a model

Inventory:

| Hook | Mechanism | Feature macro | Callers in repo |
|---|---|---|---|
| Registrar | `App::SetRegistrar(std::function<void(App&)>)` App.h:42, run inside init (App.cpp:60) | none | MinimalViewer only |
| Default layout | `SetDefaultLayout(std::function)` App.h:47 | none | nobody (App.cpp:502-508 comment admits it) |
| Open override | `SetOpenFileHandler(std::function<void()>)` App.h:68 | ONYX_HAS_OPEN_FILE_HANDLER | nobody, no test |
| Menu filter | `SetMenuEntryFilter(std::function<bool(string_view,string_view)>)` App.h:88 | ONYX_HAS_MENU_ENTRY_FILTER | nobody, no test |
| Hide Viewer window | `DocumentWindow::SetVisible(bool)` | ONYX_HAS_DOCUMENT_WINDOW_VISIBILITY | nobody |
| Folder picker | free fn `SystemOpenFolderDialog` in `Onyx::App`; its siblings `SystemOpenFileDialog`/`SystemSaveFileDialog` are in the GLOBAL namespace (UIHelpers.h) | none | nobody |

Problems:
- Zero tests and zero in-repo callers. They exist because one external host (Icon Composer, per commit messages) needed them.
- Each hook is a point fix for one symptom of one cause: the shell's menu, commands and tab host are hardcoded, so the host can only suppress. Hook 3 exists only because DocumentWindow is "not a panel" so `setPanelVisible` did not reach it (commit 23899d0). Hooks 1+2 exist because "File > Open" is a hardcoded menu item bound to a hardcoded command.
- The filter is documented as "asked once per entry, per frame, before the entry is drawn", but `menuEntryWanted` is applied only to Close All (App.cpp:400), Recent Files (419), Exit (458) and the Export menu (470). NOT to the File menu itself (393), "Open..." (396), Options (488), "Settings..." (489), View (497), Reset Layout (509), panel toggles (498). A host cannot hide those. The contract is two raw strings ("File","Close All") that break silently on rename.
- Duplicated dispatch: `if (m_openFileHandler) ... else m_requestOpenFile = true` at App.cpp:201-204 (Ctrl+O) and 396-399 (menu). Ctrl+W always calls `CloseActiveTab()` (205), F1 toggles UI Gallery (208): not hookable.
- Menu labels advertise shortcuts that are not bound: "Ctrl+," (App.cpp:489), "Ctrl+E" (479), "Ctrl+C" (483). Only Ctrl+O/W/F1 are handled (198-211).
- Feature detection: 3 macros for 6 hooks, the folder picker has none, macros are scattered across App.h and DocumentWindow.h.
- Missing and likely next: veto close (`m_wantClose` at App.cpp:352-356, 465 goes straight to `glfwSetWindowShouldClose` in Window.cpp:887-888, so a host with unsaved work cannot intercept X, File>Exit or Alt+F4); drag and drop (Window.cpp:191 `glfwSetDropCallback(..., [](...){})` is an empty lambda); add menus/commands (hosts "draw their own bar below", giving two menu bars); shortcuts; title bar content; recents labels (hardcoded gow1/gow2/ragnarok, App.cpp:427-432).

### Proposed coherent extension model
1. One versioned seam: a single `constexpr int kShellApiVersion` (and a feature bitset) instead of one `ONYX_HAS_*` macro and one `Set*` per hook.
2. Command registry: `Register({id:"file.open", title, shortcut, enabled(), run()})`. Shell registers defaults (`file.open`, `file.closeAll`, `file.exit`, `view.resetLayout`, `options.settings`). Host overrides by id or removes. Ctrl+O, menu and any palette share one path. Replaces SetOpenFileHandler.
3. Menu model separated from rendering: `Menus.Add({path:"File/Open...", command, group, order, when})`, `Remove(path)`. View menu generated from panels. Replaces SetMenuEntryFilter with typed ids, covers every entry, and lets hosts ADD (kills the two-menu-bars problem). The macOS native menu becomes a second renderer of the same model instead of the per-call if/else wrapper in NativeMenuBar.cpp.
4. Everything visible is an IPanel: make DocumentWindow a panel ("Viewer") so `setPanelVisible` works. Hook 3 disappears. PanelRegistry gets `Remove(name)` and a descriptor `{name, factory, defaultVisible, defaultDock}` so layout and visibility live with registration.
5. Platform services: `IPlatformDialogs{OpenFile, OpenFolder, SaveFile}` with a Windows default and a test fake. Hook 4 becomes a service method, one namespace, one style.
6. Lifecycle delegate with default no-ops: `onStartup()`, `onCloseRequested() -> bool`, `onFilesDropped(span<path>)`, `onShutdown()`. The 5th/6th hooks (close veto, file drop) are two virtuals, not two more setters and macros.
7. One headless shell test with a fake host asserting the above. Today there is none.

## 3. State management

Process-global mutable state (main-thread-only by convention; `ASSERT_MAIN_THREAD` exists but Window never calls `MarkMainThread()`; only host examples/tools do, Examples/MinimalViewer/Source/Main.cpp:125):
- `static Window* s_windowInstance` Window.cpp:45 (used by the refresh callback 186-189).
- `AppConfig* s_instance` AppConfig.cpp:46; set Window.cpp:136; never reset, so it dangles after `~Window` for any late `AppConfig::Get()`.
- Appearance: `s_desired, s_applied, s_env, s_dirty, s_everApplied, s_animateNext, s_baked*, s_palette*, s_transitioning` (Appearance.cpp:147-164).
- Fonts: `s_fonts, s_iconCfg, s_currentFontIndex/Size, s_monoFont, s_pendingRebuild`. `s_monoFont` is an `ImFont*` invalidated by every `io.Fonts->Clear()` (FontManager.cpp BuildAtlas).
- Frame scheduler statics (FrameScheduler.cpp:15-17). `Scale::s_nativeScale` (ScaleManager.cpp:9) duplicates `Appearance::Env().nativeScale`.
- TaskManager: 8 file-scope statics (TaskManager.cpp:11-24). EventManager: function-local statics (EventManager.h:~187-200). Logger/SessionLog/Metrics/Diagnostics/Threading statics.
- `Theme` `s_flashState` (ThemeManager.cpp:14) with `GetFlashState()` having no callers (SettingsWindow keeps its own m_flash* members).
- TitleBar mutable namespace globals `showTitleBarBackDrop`, `backdropAlpha`. NativeWindow(Windows) `s_oldWndProc, s_titleBarHeight, s_glfwWindow, s_fullFrameCallback` (NativeWindow_windows.cpp:20-22,52). `NativeMenuBar::s_enabled`. `CameraPanel::s_instance` (CameraPanel.cpp:10) with static `Toggle()`.
- Static locals as widget state: `static bool showFontDebug` (SettingsWindow.cpp:195), `static char buf[32]` (485), `static const ViewerOpener opener` (DocumentBrowser.cpp ~163), `static uint32_t crashWatchdog` (Window.cpp:897), `static ImWchar s_iconRanges[3]` (FontManager.cpp).
- Singleton facades: `Api::*` raw-pointer facade (ToolkitApi.h), `AssetVisibility::Get()`, `Logger::Get()`, `TypeCatalog::Get()`. `AppConfig::load()` mutates the global `AssetVisibility::Get()` (AppConfig.cpp ~137), so a config cannot be loaded in a test without changing process state.
- Consequence: two Windows in one process, tests, or re-init share state, which is why Window must be strict about destruction order.

Persistence is spread over five stores with different lifetimes: `onyx.toml` (AppConfig), `imgui.ini`, `onyx_recents.txt`, the `Services::Settings` TOML service (documented as "onyx-settings.toml beside onyx.toml" but only Workspace/AssetVisibility use it), and `Appearance::State`, a live copy of part of AppConfig. All paths resolve next to the executable (`PathUtils::resolvePath`: Window.cpp:124,696; App.cpp:305). A read-only install dir cannot persist, and two hosts sharing one SDK copy share config.

## 4. Persistence and config bugs

- Config saved only in `~Window` (Window.cpp:196-211). A crash, any of the 12 `std::exit(-1)` sites in Window.cpp (279, 282, 294, 362, 375, 510, 551, 560, 639, 651, 660, 667: all skip destructors), or the crash-watchdog `abort()` (908) loses every setting change of the session.
- `AppConfig::save` (AppConfig.cpp ~280): `std::ofstream f(path)` truncates in place, no temp+rename, failure to open is a silent `return`.
- `AppConfig::load` (~76): a TOML parse error silently returns defaults (`catch (const toml::parse_error&) { return cfg; }`); the next `save` overwrites the user's recoverable file with defaults. No log, no `.bak`.
- `RecentFiles::Add` rewrites the file on every Add (RecentFiles.cpp ~70). `Load` stats every entry at startup (network-path stall) and silently drops missing ones.
- Duplicate truth: SettingsWindow writes `config->accent*`, `themeMode`, `uiScale` directly AND calls Appearance (SettingsWindow.cpp:87-118, 216-217, 233-234, 374-377, 408-414). At shutdown `config.setAppearanceState(Appearance::Get())` (Window.cpp:208) overwrites those fields, so they are dead stores and the system is consistent only because of that one line. docs/design/appearance-system.md says "there is no second copy of the truth".
- `nativeDecorations` is a no-op on Windows/Linux: `Window::configureGLFW` always `GLFW_DECORATED=false` (Window_windows.cpp ~20) and `App::init` forces `borderless = true` off macOS (App.cpp:108-112), yet Settings offers the checkbox "Requires restart" (SettingsWindow.cpp:321-331). AppConfig.h comment says "custom borderless by default" for `nativeDecorations = true` (inverted).
- DPI: native scale is only computed under `__APPLE__` (Window.cpp:711-719). On Windows/Linux `nativeScale = 1.0` forever, no `glfwSetWindowContentScaleCallback`, `Scale::SetNativeScale` has no caller. Multi-monitor/150% scaling is unhandled (verify against the exe manifest).
- Font identity persisted as an absolute path under the exe dir (FontManager.cpp:72). Moving the install silently falls back. The list has two entries labeled "SF Mono (bundled)" (FontManager.cpp ~68) and the Settings combo uses label as ImGui ID (SettingsWindow.cpp:261): collision if both files exist.

## 5. Theme/appearance vs docs/design/appearance-system.md

Core is implemented as designed (State/Environment/Resolved, pure `Resolve`, single `Commit`, Appearance.cpp:224-287). Divergences:
- Doc: "Panels never touch ImGuiStyle, never call ApplyTheme". Reality: `Theme::ApplyTheme` is called 8 times from SettingsWindow (lines 87,106,116,375,412,709,752 and the transition) and from Window.cpp:701 (immediately superseded by `Appearance::Set` at 730: redundant). `ApplyTheme` is only a shim over `Appearance::Mutate` (ThemeManager.cpp:355-367).
- SettingsWindow reimplements the palette transition (m_transitioningAccent, SettingsWindow.cpp:76-120, 430-433; hand-copied cubic ease, "duration synchronized with ThemeManager", line 98). It calls `ApplyTheme(..., animate=false)` EVERY FRAME for 0.25 s, marking Appearance dirty and running the full `Resolve` (HouseStyle + BuildPalette incl. binary-search surface clamps) + whole-style assignment per frame. `Appearance` already supports `animateColors=true`; preset buttons need one call.
- Theme editor "flash" writes `ImGui::GetStyle().Colors` directly (SettingsWindow.cpp:678-757), bypassing Appearance; closing the window or a Commit mid-flash leaves a modified colour.
- ScaleManager is a vestigial shim: `Scale::Init` -> `Appearance::Mutate`; `ApplyStyleScale` has zero callers; `s_nativeScale` duplicates Appearance's. UiGallery still references it (603-604) and has a stale hint (747: "ScaleManager::ApplyStyleScale resets the whole ImGuiStyle").
- System theme never follows the OS: `ResolveMode` (ThemeManager.cpp:18) has no callers; `DetectSystemAppearance` is not called anywhere else; `systemPrefersDark` is set once at init from `Theme::GetEffectiveMode()` (Window.cpp:727), which reads the Environment default `true`. ThemeMode::System is effectively "Dark" and never updates on OS change. Platform/SystemTheme_*.cpp is dead at runtime.
- `FontManager::UploadAtlas()` is just "clear the flag" (FontManager.cpp ~219) but is still called from two places (Appearance.cpp:258-259 and Window.cpp:992-994): dead plumbing kept "so call sites need no change".
- BuildAtlas (FontManager.cpp:106-215) clears and rebuilds the whole atlas and reloads the icon TTF and mono TTF each time font path or size changes; UiGallery commits on every frame of a size drag (comment at UiGallery.cpp ~598).
- Persisted `colorOverrides.slot` is the raw `ImGuiCol_*` enum int (AppConfig.cpp ~255); an ImGui upgrade that renumbers colours corrupts user themes (no name mapping).
- ThemeManager.cpp is 595 lines, ~100 of them a hardcoded colour-group table in the Service layer.

## 6. Threading

Three concurrency/eventing systems:
1. `TaskManager` (static pool, ImHex port). NO caller creates tasks anywhere (`createTask/createBackgroundTask/doLater` appear only in TaskManager.cpp; searched Source, Include, Examples, Tools, Tests). Yet Window ctor starts `max(2, hardware_concurrency)` idle workers (TaskManager.cpp:158-200 via Window.cpp:174), `App::frame` calls `runDeferredCalls`/`collectGarbage` every frame (App.cpp:165-166), and StatusBar draws progress UI for tasks that cannot exist (StatusBar.cpp:75-105).
2. `JobQueue` in Workspace (lane-serialized, `Pump()` on UI thread, Window.cpp:937): the real worker path.
3. `EventManager` (global, mutex, ImHex port) AND `EventBus` (per-Workspace, Pump()'d): two pub/sub systems. `EventManager::post` holds a global recursive mutex while calling subscribers and iterating a `std::multimap` (EventManager.h ~148-160). A subscriber that unsubscribes during dispatch erases a node of the range being iterated = UB. Window posts FrameTick/FrameEnd every frame (App.cpp:170,253); nothing in this repo subscribes to them (only EventAnimationLoaded has subscribers), so it is 2 lock + multimap lookups per frame for nothing.

Races/hazards in TaskManager (public API; StatusBar reads it):
- `getRunningTasks()` returns `s_tasks` by reference with no lock (TaskManager.cpp:283-285), iterated on the UI thread (StatusBar.cpp:75) while `createTask` (any thread) `emplace_back`s under `s_queueMutex`: data race on a `std::list`.
- `collectGarbage` reads `s_tasks.empty()` outside the lock (275). `runWhenTasksFinished` iterates `s_tasks` under `s_tasksFinishedMutex`, not `s_queueMutex` (315-318). `exit()` iterates unlocked (205).
- `Task::m_interruptCallback` (plain std::function) is read in `interrupt()` (78-82, any thread), written in `setInterruptCallback` (84-86) and read in `exception()` (113) with no common lock.
- `runDeferredCalls` holds `s_deferredCallsMutex` while running callbacks and loops until empty (306-313): a callback that calls `doLater` spins; a callback that waits on a worker that itself calls `doLater` deadlocks.
- UI/worker boundary for Documents is clear and documented ("never read a Document before TreeReady", `doc.ready` atomic; DocumentBrowser.cpp ~118). Everything else is by convention only.
- Modal dialog inside the frame: `App::drawPopups -> handleOpenFileRequest -> SystemOpenFileDialog` (App.cpp:257-267, 308) blocks the UI thread mid-frame. The parent still gets WM_PAINT; GLFW's refresh callback is wired to `fullFrame()` (Window.cpp:186-189) with NO re-entrancy guard (894-913). A nested `fullFrame` calls `ImGui::NewFrame` inside an open frame: assert in debug, in release swallowed by `catch (...)` (906-911) and printed to stderr only. Plausible source of release-only glitches (verify by repro). `setFullFrameCallback` is never called (all 3 platform impls), so the WM_MOVE path in NativeWindow_windows.cpp:56-58 is dead.

## 7. ImGui coupling and widgets

- Raw ImGui dominates. `ImGui::` call counts: SettingsWindow 203 (Widgets:: 4), UiGallery 286 (Widgets:: 27), Dopesheet 43, StatusBar 43, TitleBar 42, CameraPanel 42, AnimCurveView 34. 41 uses of `Widgets::` across Source versus 49 direct `ImGui::Button/SmallButton/Selectable/CollapsingHeader/MenuItem/BeginTabItem` calls in App+Viewers: the wrapper is bypassed about as often as it is used.
- Widgets.cpp is not an abstraction layer: 6 contrast-aware wrappers + IconButton + ColoredTreeNode. It predicts the next item's rect and tests hover before drawing (Widgets.cpp ~14-40), which is wrong under clipping, scrolling, docking overlap and disabled state. Non-wrapped widgets get no contrast fix, so readability is inconsistent. Fix at theme level (derive text per surface in `BuildPalette`) instead of per-widget hover prediction.
- Heavy `imgui_internal.h` use (Window, App, SettingsWindow, TitleBar, Widgets, NativeWindow). Windows hit-testing reads `GImGui->HoveredWindow->Name` from the WndProc and strcmp's "##HostWindow"/"OnyxDockSpace" (NativeWindow_windows.cpp ~137-140): renaming the host window silently breaks title bar drag.
- SettingsWindow is hard-wired to renderer config (bgTop..matcap). Four of those fields are persisted, shown disabled with "Not wired up yet", and have no reader (SettingsWindow.cpp:535-584).
- UiGallery (861 lines) is a core panel registered unconditionally (App.cpp:48) with global F1 (208): a dev tool (ImGui demo, metrics, style editor) compiled into every host.
- Header hygiene: `UIHelpers.h` does `using Onyx::App::HashHex; ...` at global scope; `Services/PathUtils.h` (public) includes `<windows.h>` and defines global `namespace PathUtils`; dialog functions mix global and `Onyx::App` namespaces.

## 8. Per-frame / weight smells (ranked)

1. StatusBar Log copies the entire ring every frame: `auto entries = Logger::Get().GetEntries();` (StatusBar.cpp:123) copies up to 4096 `LogEntry{level,time,message}` under the logger mutex, then does `PushID` + `snprintf` + `Selectable` per entry with no `ImGuiListClipper` (128-181). Logger ring trims with `vector::erase(begin(),...)` (Logger.cpp:95-97), O(n) per line at cap. Log is a default panel (App.cpp:44). Worker-thread logging contends with the UI copy.
2. DocumentBrowser draws every root of every document each frame, unclipped (DocumentBrowser.cpp ~100-209): thousands of `ColoredTreeNode` + `Catalog().Icon/Color` lookups + `snprintf` per frame for flat archive roots. Children are lazy; roots are not. Double-click decodes synchronously on the UI thread (comment ~145).
3. Per-frame Win32 style writes: `NativeWindow::beginFrame` does `GetWindowLong/SetWindowLong` for `GWL_STYLE` and `GWL_EXSTYLE` (`WS_EX_COMPOSITED`) EVERY frame (NativeWindow_windows.cpp ~190-205 via WindowDecorator::beginFrame, App.cpp:215). `WS_EX_COMPOSITED` is known to interact badly with flip-model/Vulkan swapchains. Set once in `setup()`.
4. Appearance churn during accent transition and colour-picker drag (section 5).
5. Idle pacing: `glfwWaitEventsTimeout(1/15)` (Window.cpp:875). Only `Appearance` and Window (mac resize) call the Frame scheduler; nothing in Viewers does (grep). StatusBar's indeterminate progress bar (StatusBar.cpp:87-91) uses `ImGui::GetTime()` without `RequestAnimation`, so it steps at idle rate. Check whether video/3D playback relies on active-item/input to stay fast.
6. Minor per-frame allocations: `TitleBar::draw(const std::string&)` receives `windowTitle.c_str()` (App.cpp:352-354) so a std::string is built every frame; `std::string(p->getName()).c_str()` per panel while View is open (App.cpp:499); linear `PanelRegistry::find`; user std::function called several times/frame by `menuEntryWanted`.
7. Idle TaskManager workers + 2 EventManager posts per frame (section 6). Startup stats in RecentFiles::Load / FontManager::Init.

## 9. Bug and dead-code smells (file:line)

- Window.cpp:906-911: release builds swallow every frame exception (only `fprintf(stderr)`, invisible in a GUI exe); Debug does not catch, so behaviour differs by config. `abort()` after 10 failures (908) bypasses SessionLog uninstall.
- Window.cpp:191: empty drop callback. Window.cpp:136/259: `AppConfig::SetInstance` never cleared; GLFW callbacks not unregistered before `glfwDestroyWindow`.
- Window.cpp (12 sites): init failures = `fprintf` + `std::exit(-1)` from a constructor, no dialog, SessionLog (opened line 133) never closed on these paths.
- Window.cpp:303-325 vs 198-204: window geometry has two writers (callbacks and `~Window`).
- App.cpp:235-236 duplicated comment; 427-438 dead commented label and hardcoded gow1/gow2/ragnarok -> GOW1/GOW2/GOWR in an engine that calls itself game-agnostic (also RecentFiles.h:13, UIHelpers.h:18, macos_menu.m `GOWMenuItemHandler`).
- App.cpp:470-486: Export menu, three always-disabled empty items shipped to all hosts (own comment: "none had a working export body").
- App.cpp:502-508: stale comment says SetDefaultLayout is never set, but the code at 224-230 uses it.
- App.cpp:308: `drawPopups()` is only `handleOpenFileRequest()`.
- App.cpp:283-302 / 257-281: `openRecentFile` and Open fail silently (missing file stays in recents; "no module accepts" only logged: the very "warning nobody reads" the hook-1 commit complains about).
- WindowDecorator::drawTitleBar is unused (App calls TitleBar directly); `init`'s second arg is ignored; Portuguese comments describe a flow (BeginMainMenuBar) the code no longer follows.
- Empty platform stubs: `Window::setupNativeWindow/beginNativeWindowFrame/endNativeWindowFrame` on Windows; `NativeWindow_linux.cpp` is 15 lines of no-ops.
- IPanel.h:5 `struct AppContext;` forward decl for a type that does not exist.
- CameraPanel.cpp:29-30 writes `cfg->camPanelVisible` every frame (panel in the engine tied to config by singleton).
- Comments: mixed PT/EN, large "fix-round" history blocks (e.g. Window.cpp:48-56 "T10 fix-round-1 (MEDIUM)") make Window.cpp about half prose.

## 10. Top recommendations (impact order)

1. Split Window: `VulkanPresenter` (swapchain/sync/present), `PlatformWindow` (GLFW + native), thin composition root. Replace `exit(-1)` with one fatal-error path.
2. Replace hook setters with command/menu/panel/delegate model + one API version; make DocumentWindow a panel; add shell test.
3. Make `Appearance` the sole writer: delete SettingsWindow's transition and direct config writes, delete ScaleManager and Theme shims, move flash into Appearance, wire OS-theme and DPI changes into `Environment`.
4. Persist safely: debounced save-on-change, atomic write, log+backup on parse error, host-supplied config dir; remove `AppConfig::Get()` from Render/Viewers (pass an interface), clear on shutdown.
5. Remove TaskManager (unused) or make it the only pool and lock it; unify EventManager/EventBus; add re-entrancy guard to `fullFrame`; do not run modal dialogs inside the frame.
6. Virtualize Log and DocumentBrowser lists.
7. Windows: set style bits once, re-evaluate WS_EX_COMPOSITED with Vulkan.
8. Prune: game strings, UiGallery default registration, Export stubs, dead functions listed in section 9.
