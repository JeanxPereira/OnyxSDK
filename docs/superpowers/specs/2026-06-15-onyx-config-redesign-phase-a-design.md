# Onyx Config Redesign — Phase A (window chrome + layout) Design

> Phase A of a two-phase OnyxSDK config/persistence redesign driven by SCUMMRedux. **Phase A (this spec)** delivers the visible quick wins without touching the config *format*: fix the OS/taskbar window title, let ImGui own its own native layout file (which gives **layout autosave for free**), and let each app define its **default dock layout** (e.g. SCUMMRedux docks "Rooms" on the right). **Phase B (deferred)** migrates the app config from the binary `GTKC` format to a human-readable, extensible text format (TOML/YAML/JSON), with per-app files and app-extensible settings.

## Context & goals

OnyxSDK's app config is a binary `GTKC` file that (a) embeds the ImGui dock layout as an opaque blob, (b) is saved only on exit (no autosave), (c) hardcodes a GoW-shaped default dock layout in the engine, and (d) never updates the OS window title after creation. SCUMMRedux's GUI exposed all four. Phase A fixes the three visible ones (title, layout-control, autosave) by **decoupling the ImGui layout from the config** — which is independent of, and a prerequisite for, the Phase B format migration. Goal #2 (evolve Onyx into a generic base) is advanced: the GoW-specific default layout moves out of the engine into the GoWToolkit app.

## Founding decisions (user-approved)

1. **Phase A first, Phase B (format migration) next.**
2. **ImGui owns its native `.ini`** (set `io.IniFilename` to a real per-exe path) — removes the GTKC imgui-blob round-trip and gives native autosave/auto-restore of the layout.
3. **Per-app default layout** via an `App::SetDefaultLayout` callback; the engine ships no GoW layout.
4. **OS title sync** so the taskbar matches the in-app titlebar.

## Current state (audited — exact sites)

- Config: binary `GTKC` v11, single shared file `gowtool.gtkc` (`Source/Window/Window.cpp:44`, via `PathUtils::resolvePath`). Saved **only** in the `Window` destructor (`Window.cpp:94`); no autosave.
- ImGui layout: `io.IniFilename = nullptr` (`Window.cpp:184`); engine captures the layout via `ImGui::SaveIniSettingsToMemory` in the destructor (`Window.cpp:88-92`) into `AppConfig.imguiIniState`, embeds it in GTKC, and restores via `LoadIniSettingsFromMemory` (`Window.cpp:187-189`). This blob round-trip is the coupling to break.
- Default layout: `App::setupDockLayout` (`Source/App.cpp:190-215`) hardcodes GoW panels ("PAK Browser"/"WAD Browser"/"Inspector"/"Camera"/"Log"); gated by `m_layoutInitialized` (`App.cpp:159-164`); "Reset Layout" sets `m_layoutInitialized=false` (`App.cpp:496`).
- Title: `glfwCreateWindow(..., m_config.windowTitle.c_str(), ...)` at construction (`Window.cpp:129-130`); custom titlebar reads `m_config->windowTitle` each frame (`App.cpp:379`); **no `glfwSetWindowTitle` anywhere** → OS/taskbar title stuck at the create-time default ("Onyx Toolkit") while the registrar sets the real title later.

## Design

### OnyxSDK (engine)

1. **OS title sync.** In the frame path (or `App::frame`), keep a `std::string m_lastAppliedTitle`; when `m_config->windowTitle != m_lastAppliedTitle`, call `glfwSetWindowTitle(m_window, m_config->windowTitle.c_str())` and update `m_lastAppliedTitle`. This picks up the registrar's title (set during `App::init`) on the next frame and any later change. The custom titlebar already shows it; this fixes the taskbar.

2. **ImGui owns its native ini.** Set `io.IniFilename` to a persistent, exe-relative path (`PathUtils::resolvePath("imgui.ini")` — naturally per-app since each consumer is its own exe in its own dir; store the resolved string so the `const char*` stays valid for ImGui's lifetime). Optionally set `io.IniSavingRate` for responsiveness. **Remove** the GTKC imgui-blob round-trip: drop the `SaveIniSettingsToMemory`→`imguiIniState` capture in the destructor and the `LoadIniSettingsFromMemory` restore in `initImGui`. ImGui now auto-loads `imgui.ini` at startup and auto-saves on change (and on shutdown) → layout autosave, free. The `AppConfig.imguiIniState` field becomes vestigial (left in place; Phase B removes it; GTKC still persists the non-layout fields).

3. **Per-app default layout.** Add `App::SetDefaultLayout(std::function<void(ImGuiID dockspaceId)>)`. Replace the `m_layoutInitialized`/hardcoded-`setupDockLayout` logic with: each frame, after computing `dockspace_id`, if `ImGui::DockBuilderGetNode(dockspace_id) == nullptr` (no layout loaded from `imgui.ini` and none built yet) and a default-layout callback is set, invoke it to build the initial split; otherwise leave ImGui's loaded layout intact. Remove the GoW-specific body of `setupDockLayout` from the engine (the engine ships no game layout; with no callback, panels use ImGui's default docking). "Reset Layout" clears the current dock node(s) (`DockBuilderRemoveNode`) so the callback rebuilds on the next frame.

### GoWToolkit (no regression)

In its registrar, call `app.SetDefaultLayout(...)` with the current GoW split (port the existing `setupDockLayout` body verbatim: PAK/WAD left ~22%, Inspector/Camera right ~25%, Log bottom ~20%, Viewer center). Net UX: identical. (Title already set via `windowTitle`.)

### SCUMMRedux

In its registrar, call `app.SetDefaultLayout(...)` to dock the **"Rooms"** panel on the right (e.g. split right ~22%, dock "Rooms" there; document/viewers in the center). Title + accent already set.

## Data flow

- **Startup layout:** ImGui loads `imgui.ini` if present → existing layout restored (autosaved from last session). If absent (first run / post-Reset), `DockBuilderGetNode==null` → engine calls the app's `SetDefaultLayout` callback → initial split built; ImGui then autosaves it.
- **Autosave:** ImGui marks the ini dirty on dock/resize/move and writes `imgui.ini` per `IniSavingRate` and on shutdown — no engine save call needed for layout. (Non-layout config — window geom, accent, theme — stays in GTKC, saved on exit as today; Phase B revisits.)
- **Title:** registrar sets `windowTitle` during `App::init` → next frame the title-sync applies it to the GLFW/OS window.

## Testing

- **OnyxSDK:** builds standalone; headless tests green; `MinimalViewer` builds + `--selftest` green; manual: MinimalViewer launches with ImGui's default docking (no GoW layout, no crash), `imgui.ini` is created next to the exe and persists a moved panel across relaunch.
- **GoWToolkit:** builds against local OnyxSDK; golden GOW2/GOWR green. **User manual check:** same panels/layout as before, OS title "God Of War Toolkit", layout persists after rearranging.
- **SCUMMRedux:** `scummredux-gui --selftest` green. **User manual check:** "Rooms" docked on the right, OS/taskbar title "SCUMMRedux", layout persists across relaunch after rearranging (autosave).

## Sequencing & release

Same release dance as v0.3.0: make OnyxSDK changes on `main`; iterate GoWToolkit + SCUMMRedux against the local OnyxSDK (`-DFETCHCONTENT_SOURCE_DIR_ONYXSDK=...`) to verify; cut OnyxSDK **v0.4.0**; bump both consumers' `GIT_TAG` to v0.4.0 and rebuild without override. Consumer panel/layout registrar edits land on each consumer's branch.

## Out of scope (Phase B, deferred)

- Migrating the app config from binary `GTKC` → a human-readable, extensible text format (format choice TOML/YAML/JSON to be decided in Phase B).
- Per-app config file naming (vs the shared `gowtool.gtkc`).
- App-extensible config settings (apps adding their own persisted keys without engine edits).
- Removing the now-vestigial `AppConfig.imguiIniState` field (Phase B, with the format migration).

## Open questions / to verify in the plan

1. **`imgui.ini` location/name:** exe-relative `imgui.ini` via `PathUtils::resolvePath` (per-exe = per-app). Confirm the resolved string's lifetime satisfies ImGui's `io.IniFilename` (must outlive the context — store in a long-lived member/static).
2. **Title-sync placement:** `App::frame` vs end of `App::init`. Per-frame `if changed` is robust to later changes; confirm it's cheap (string compare) and that `m_window` is valid there.
3. **Reset Layout mechanics:** `DockBuilderRemoveNode(dockspace_id)` then rebuild via callback next frame; verify it fully clears docked windows (may need to also clear the saved `imgui.ini` or call `DockBuilderRemoveNodeChildNodes`).
4. **No-callback default:** with no `SetDefaultLayout` (e.g. MinimalViewer), confirm ImGui's default docking is acceptable (panels dock to the central node / float) and nothing asserts.
