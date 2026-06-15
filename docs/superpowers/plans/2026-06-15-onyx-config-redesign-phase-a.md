# Onyx Config Redesign Phase A — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the OS/taskbar window title, let ImGui own its native layout file (free layout autosave), and let each app define its default dock layout — without touching the binary config format (that's Phase B). After the engine change, GoWToolkit keeps its exact layout and SCUMMRedux docks "Rooms" on the right.

**Architecture:** OnyxSDK changes: (1) per-frame OS-title sync; (2) `io.IniFilename` → a real `imgui.ini`, removing the GTKC imgui-blob round-trip; (3) an `App::SetDefaultLayout(callback)` invoked when no layout is loaded, with the GoW-specific default layout moved out of the engine. Then GoWToolkit and SCUMMRedux each register their default layout. Release OnyxSDK v0.4.0 and bump both consumers.

**Tech Stack:** C++20, CMake + Ninja/VS, MSVC, ImGui (docking). Repos (siblings): `C:\CodingProjects\Personal\OnyxSDK` (engine, branch `main`, tag `v0.3.0`), `C:\CodingProjects\Personal\GoWToolkit` (consumer, branch `feat/onyx-genericization`, consumes v0.3.0), `C:\CodingProjects\Personal\SCUMMRedux` (consumer, branch `main`, consumes v0.3.0).

**Design spec:** `OnyxSDK/docs/superpowers/specs/2026-06-15-onyx-config-redesign-phase-a-design.md`.

**Audited sites (verify by reading before editing):** `Source/Window/Window.cpp` — config path `:44`, save `:94`, glfwCreateWindow `:129-130`, imgui capture (destructor) `:88-92`, `io.IniFilename=nullptr` + restore `:184-189`. `Source/App.cpp` — dockspace init `:159-164`, `setupDockLayout` `:190-215`, titlebar draw `:379`, reset-layout `:496`. `Include/Onyx/App/App.h` — public API + private members. (Line numbers are from the audit; confirm before each edit.)

---

## Environment

VS 2022 BuildTools dev shell, SAME shell call as the build; `Set-Location` per repo:
```powershell
Import-Module "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" -DevCmdArguments "-arch=x64 -host_arch=x64" -SkipAutomaticLocation
```
- OnyxSDK standalone: `Set-Location C:\CodingProjects\Personal\OnyxSDK ; cmake -B build ; cmake --build build ; ctest --test-dir build --output-on-failure`. After engine tasks, run `MinimalViewer --selftest <any file>` (exit 0).
- Consumers iterate against the LOCAL engine: `cmake -B build -DFETCHCONTENT_SOURCE_DIR_ONYXSDK=C:/CodingProjects/Personal/OnyxSDK ; cmake --build build ; ctest --test-dir build --output-on-failure`.
- New files need a `cmake -B build` reconfigure; if ctest needs a config add `-C Debug`. First engine-linked consumer build is heavy (~10 min).

**Commit hygiene:** `git add` explicit paths only (never `git add -A`); **no AI-credit trailer**; never commit a red tree. Each repo commits on its own branch. **GoWToolkit has pre-existing unrelated dirty files (third_party submodules + an untracked `tests/fixtures/minimal_v8.lecf`) — never stage them.**

**Verification reality:** this is a GUI/UX change. Automated net = builds + existing tests + MinimalViewer `--selftest` green. The *visible* results (taskbar title; Rooms-on-right; layout persists across relaunch) are confirmed by the user launching the apps. **Do NOT launch GUI windows from a headless agent.**

**STOP-before-release:** Implement **Tasks 1–5 only**. STOP before Task 6 (tag/push/bump) — Task 6 runs after the user's manual GUI acceptance.

---

## Task 1 (OnyxSDK): OS/taskbar title sync

Update the GLFW window title whenever `m_config->windowTitle` changes, so the taskbar matches the in-app titlebar.

**Files:** `Include/Onyx/App/App.h`, `Source/App.cpp`.

- [ ] **Step 1: Add a tracking member to App.h**

In `Include/Onyx/App/App.h`, in the private members section (near `bool m_layoutInitialized;`), add:
```cpp
    std::string m_lastAppliedTitle;   // last title pushed to glfwSetWindowTitle
```
(`<string>` is already included.)

- [ ] **Step 2: Sync the title each frame**

Read `Source/App.cpp` `App::frame()` (the per-frame method). Near the top of `App::frame()` (it has `m_window` + `m_config`), add:
```cpp
    // Keep the OS/taskbar title in sync with the app-set config title.
    if (m_window && m_config && m_config->windowTitle != m_lastAppliedTitle) {
        glfwSetWindowTitle(m_window, m_config->windowTitle.c_str());
        m_lastAppliedTitle = m_config->windowTitle;
    }
```
(`<GLFW/glfw3.h>` is already included by App.h.) This applies the registrar-set title on the first frame after `App::init` and any later change.

- [ ] **Step 3: Build + verify**

`Set-Location C:\CodingProjects\Personal\OnyxSDK ; cmake --build build ; ctest --test-dir build --output-on-failure` → green. (Visible effect confirmed later by the user; nothing headless to assert here beyond a clean build.)

- [ ] **Step 4: Commit (OnyxSDK `main`)**
```
git add Include/Onyx/App/App.h Source/App.cpp
git commit -m "fix(app): Sync OS window title with config title"
```

---

## Task 2 (OnyxSDK): ImGui owns its native `imgui.ini`

Point ImGui at a real per-exe ini file and remove the GTKC imgui-blob round-trip, so ImGui auto-loads/saves the layout (free autosave).

**Files:** `Source/Window/Window.cpp`.

- [ ] **Step 1: Set `io.IniFilename` to a persistent path**

Read `Source/Window/Window.cpp` around `initImGui` (`:174-190`). Replace the `io.IniFilename = nullptr;` line (`:184`) with a real, exe-relative path whose backing string outlives the ImGui context. Add a long-lived member to the `Window` class (in `Window.h`) `std::string m_imguiIniPath;`, then in `initImGui`:
```cpp
    m_imguiIniPath = Onyx::Services::PathUtils::resolvePath("imgui.ini").string(); // or .c_str() per resolvePath's return type
    io.IniFilename = m_imguiIniPath.c_str();
```
(Confirm `PathUtils::resolvePath` is already used in this file at `:44`; match its return type — `.string()` if it returns `std::filesystem::path`.) Optionally set `io.IniSavingRate = 2.0f;` for snappier autosave.

- [ ] **Step 2: Remove the imgui-blob restore**

In `initImGui`, delete the block (`:187-189`) that restores from the embedded blob:
```cpp
    if (!m_config.imguiIniState.empty())
        ImGui::LoadIniSettingsFromMemory(m_config.imguiIniState.c_str(), m_config.imguiIniState.size());
```
ImGui now auto-loads `imgui.ini` itself (when `io.IniFilename` is set, it loads on first frame if the file exists).

- [ ] **Step 3: Remove the imgui-blob capture in the destructor**

In the `Window` destructor (`:88-92`), delete the capture into `m_config.imguiIniState`:
```cpp
    size_t len = 0;
    const char* iniString = ImGui::SaveIniSettingsToMemory(&len);
    if (iniString && len > 0) m_config.imguiIniState = std::string(iniString, len);
```
Keep the surrounding window-geometry capture + `m_config.save(m_configPath)` (the non-layout config still persists via GTKC for now — Phase B revisits). ImGui auto-saves `imgui.ini` on shutdown (and per `IniSavingRate`). Leave the now-unused `AppConfig.imguiIniState` field in place (vestigial; Phase B removes it).

- [ ] **Step 4: Build + verify**

`cmake --build build ; ctest --test-dir build --output-on-failure` → green. Then `MinimalViewer --selftest <file>` → exit 0. Manual (optional, user): launch MinimalViewer, move a panel, relaunch → `imgui.ini` exists next to the exe and the layout persisted.

- [ ] **Step 5: Commit (OnyxSDK `main`)**
```
git add Source/Window/Window.cpp Source/Window/Window.h
git commit -m "refactor(window): Let ImGui own its native imgui.ini (free layout autosave)"
```
(Adjust paths to the actual Window header/source names if different.)

---

## Task 3 (OnyxSDK): Per-app default layout callback

Add `App::SetDefaultLayout`, invoke it when no layout is loaded, and remove the GoW-specific default layout from the engine.

**Files:** `Include/Onyx/App/App.h`, `Source/App.cpp`.

- [ ] **Step 1: Add the API + member to App.h**

In `Include/Onyx/App/App.h`:
- Ensure `#include <functional>` (already present) and that `ImGuiID` is visible (`#include "imgui.h"` is present).
- In the public section (near `SetRegistrar`), add:
```cpp
    // App-provided default dock layout, invoked once when there is no saved
    // layout (fresh imgui.ini) — the engine ships no game-specific layout.
    using LayoutFn = std::function<void(ImGuiID dockspaceId)>;
    void SetDefaultLayout(LayoutFn fn) { m_defaultLayout = std::move(fn); }
```
- In the private members, add:
```cpp
    LayoutFn m_defaultLayout;
    bool     m_resetLayout = false;
```

- [ ] **Step 2: Replace the dockspace-init logic**

In `Source/App.cpp`, at the dockspace init (`:159-164`), replace the `m_layoutInitialized`/`setupDockLayout` logic with a node-presence check that calls the app callback:
```cpp
    ImGuiID dockspace_id = ImGui::GetID("OnyxDockSpace");
    if (m_resetLayout) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        m_resetLayout = false;
    }
    if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr && m_defaultLayout) {
        m_defaultLayout(dockspace_id);
    }
    ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);
```
(`imgui_internal.h` — which declares `DockBuilder*` — is already included in `App.cpp` since `setupDockLayout` used it. Keeping the `GetID` string as `"OnyxDockSpace"` is fine since we're moving to a fresh `imgui.ini`; if you prefer zero churn, keep the original `"GoWToolDockSpace"` string — just be consistent. The dockspace_id passed to the callback matches the `DockSpace` call regardless.)

- [ ] **Step 3: Remove the engine's GoW default layout**

Delete the body of `App::setupDockLayout` (`:190-215`) and its declaration in `App.h` (it is no longer called — the app callback replaces it). If removing the method entirely is cleaner, do so (remove decl + def). The GoW layout is ported to GoWToolkit in Task 4. Remove the now-unused `m_layoutInitialized` member if nothing else references it (grep first).

- [ ] **Step 4: Fix "Reset Layout"**

At the reset-layout menu handler (`:496`, currently `m_layoutInitialized = false;`), replace with:
```cpp
    m_resetLayout = true;   // triggers DockBuilderRemoveNode + default-layout rebuild next frame
```

- [ ] **Step 5: Build + verify**

`cmake --build build ; ctest --test-dir build --output-on-failure` → green; `MinimalViewer --selftest <file>` → exit 0. (MinimalViewer sets no layout callback → ImGui default docking; confirm no crash/assert at startup.)

- [ ] **Step 6: Commit (OnyxSDK `main`)**
```
git add Include/Onyx/App/App.h Source/App.cpp
git commit -m "feat(app): Add per-app default layout callback; drop engine GoW layout"
```

---

## Task 4 (GoWToolkit): Register its default layout

Port the engine's former GoW layout into GoWToolkit's registrar so its UX is unchanged.

**Files:** `Apps/GoWToolkit/Source/AppRegistration.cpp` (branch `feat/onyx-genericization`).

- [ ] **Step 1: Add the layout callback**

In `AppRegistration.cpp`, add `#include "imgui_internal.h"` (for `DockBuilder*`). Inside the `SetRegistrar` lambda, call `a.SetDefaultLayout(...)` with the **verbatim body of the engine's former `App::setupDockLayout`** (the DockBuilder sequence you removed in Task 3 — RemoveNode/AddNode/SetNodeSize, the three splits, and the `DockBuilderDockWindow` calls for "PAK Browser"/"WAD Browser"/"Viewer"/"Inspector"/"Camera"/"Log", then `DockBuilderFinish`). Example skeleton (fill with the exact ported body):
```cpp
    a.SetDefaultLayout([](ImGuiID dockspace) {
        ImGui::DockBuilderRemoveNode(dockspace);
        ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->Size);
        ImGuiID dock_main = dockspace, dock_left, dock_right, dock_bottom;
        ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left,  0.22f, &dock_left,   &dock_main);
        ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down,  0.20f, &dock_bottom, &dock_main);
        ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.25f, &dock_right,  &dock_main);
        ImGui::DockBuilderDockWindow("PAK Browser", dock_left);
        ImGui::DockBuilderDockWindow("WAD Browser", dock_left);
        ImGui::DockBuilderDockWindow("Viewer",      dock_main);
        ImGui::DockBuilderDockWindow("Inspector",   dock_right);
        ImGui::DockBuilderDockWindow("Camera",      dock_right);
        ImGui::DockBuilderDockWindow("Log",         dock_bottom);
        ImGui::DockBuilderFinish(dockspace);
    });
```
(Use the exact split ratios/window names from the engine's original `setupDockLayout` — if they differ from above, prefer the original.)

- [ ] **Step 2: Build against local OnyxSDK + verify**
```
Set-Location C:\CodingProjects\Personal\GoWToolkit
cmake -B build -DFETCHCONTENT_SOURCE_DIR_ONYXSDK=C:/CodingProjects/Personal/OnyxSDK ; cmake --build build ; ctest --test-dir build --output-on-failure
```
→ green (golden GOW2/GOWR). User manual check (later): panels/layout identical to before; taskbar title "God Of War Toolkit".

- [ ] **Step 3: Commit (GoWToolkit `feat/onyx-genericization`)**
```
git add Apps/GoWToolkit/Source/AppRegistration.cpp
git commit -m "feat(app): Provide default dock layout after Onyx layout-callback change"
```

---

## Task 5 (SCUMMRedux): Register "Rooms"-on-the-right layout

**Files:** `Source/Gui/ScummGuiApp.cpp` (branch `main`).

- [ ] **Step 1: Add the layout callback**

In `Source/Gui/ScummGuiApp.cpp`, add `#include "imgui_internal.h"` (for `DockBuilder*`). Inside the `SetRegistrar` lambda in `InstallScummPanels` (alongside the existing `addPanel(ScummTreePanel)` + title/accent), add:
```cpp
        a.SetDefaultLayout([](ImGuiID dockspace) {
            ImGui::DockBuilderRemoveNode(dockspace);
            ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->Size);
            ImGuiID dock_main = dockspace, dock_right;
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.22f, &dock_right, &dock_main);
            ImGui::DockBuilderDockWindow("Rooms", dock_right);   // ScummTreePanel's ImGui window name
            ImGui::DockBuilderFinish(dockspace);
        });
```
(Confirm `ScummTreePanel::Draw` calls `ImGui::Begin("Rooms")` so the name matches.)

- [ ] **Step 2: Build against local OnyxSDK + verify**
```
Set-Location C:\CodingProjects\Personal\SCUMMRedux
cmake -B build -DFETCHCONTENT_SOURCE_DIR_ONYXSDK=C:/CodingProjects/Personal/OnyxSDK ; cmake --build build ; ctest --test-dir build --output-on-failure
.\build\<config?>\scummredux-gui.exe --selftest "C:\CodingProjects\Personal\The Curse of Monkey Island\ScummVM\monkey3\COMI.LA0"
```
→ ctest green; `--selftest` exit 0. User manual check (later): "Rooms" docked right, taskbar title "SCUMMRedux", layout persists across relaunch after rearranging.

- [ ] **Step 3: Commit (SCUMMRedux `main`)**
```
git add Source/Gui/ScummGuiApp.cpp
git commit -m "feat(gui): Dock Rooms panel on the right via default layout"
```

---

## Task 6 (after user acceptance): Release v0.4.0 + bump consumers

**Do NOT start until the user has manually accepted both apps (title/layout/autosave).** This step pushes a public release.

- [ ] **Step 1:** Confirm OnyxSDK `main` is green standalone + MinimalViewer `--selftest` ok; push `main` and tag:
```
Set-Location C:\CodingProjects\Personal\OnyxSDK
git push origin main
git tag -a v0.4.0 -m "v0.4.0 - OS title sync, ImGui-owned layout (autosave), per-app default layout"
git push origin v0.4.0
```
- [ ] **Step 2:** Bump SCUMMRedux `CMakeLists.txt` OnyxSDK `GIT_TAG v0.3.0 → v0.4.0`; rebuild WITHOUT the override (`cmake -B build ; cmake --build build ; ctest ...`) → green; `--selftest` ok. Commit `build(deps): Bump OnyxSDK to v0.4.0`.
- [ ] **Step 3:** Bump GoWToolkit `CMakeLists.txt` OnyxSDK `GIT_TAG → v0.4.0`; rebuild without override → golden green. Commit `build(deps): Bump OnyxSDK to v0.4.0` (only `CMakeLists.txt`).

---

## Verificação final

- [ ] **Step 1:** OnyxSDK builds standalone, tests + MinimalViewer `--selftest` green; engine has no GoW layout; `imgui.ini` is written next to the exe.
- [ ] **Step 2 (user):** GoWToolkit — taskbar title correct, layout identical to before, rearranged layout persists across relaunch.
- [ ] **Step 3 (user):** SCUMMRedux — "Rooms" docked right, taskbar title "SCUMMRedux", layout persists across relaunch (autosave proven).

---

## Roadmap (Phase B, deferred)

- Migrate app config binary `GTKC` → human-readable extensible text (TOML/YAML/JSON — choice TBD in Phase B), per-app config file naming, app-extensible settings, and remove the vestigial `AppConfig.imguiIniState`.
- Earlier deferred goal-#2 items still open: profile-driven Open dialog (de-hardcode the GoW game list); move the engine's `PakBrowser` auto-show coupling out of `App::drawOpenDialog`.

## Self-review notes (coverage vs spec)

- Spec "OS title sync" → Task 1. ✓
- Spec "ImGui owns native ini; remove GTKC blob round-trip; free autosave" → Task 2. ✓
- Spec "per-app default layout callback; engine ships no GoW layout; reset rebuilds" → Task 3 (+ Task 4 ports GoW layout out). ✓
- Spec "GoWToolkit no regression" → Task 4. ✓
- Spec "SCUMMRedux Rooms-on-right" → Task 5. ✓
- Spec "release v0.4.0 + bumps; iterate via override; stop before release for manual acceptance" → Task 6 (gated). ✓
- Spec open questions (io.IniFilename lifetime → Window member string; title-sync in App::frame; reset via DockBuilderRemoveNode; no-callback default = ImGui default) → addressed in Tasks 1-3. ✓
```
