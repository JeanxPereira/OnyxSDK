# Onyx Panel Composition + App Chrome Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make OnyxSDK's generic panel set **app-composable** (engine registers only a minimal core; the other 6 generic panels become public + opt-in), make the **window title configurable** per app, and have each app set its own **accent** — without regressing GoWToolkit, and giving SCUMMRedux a clean slate (only its "Rooms" panel + core).

**Architecture:** A cross-repo refactor. OnyxSDK: add a runtime `AppConfig.windowTitle`; promote 6 panel headers to the public `Include/Onyx/App/Panels/` surface; reduce `App::registerPanels()` to `StatusBar` + `SettingsWindow`. GoWToolkit: its registrar re-adds the 6 generic panels (now opt-in) + their default-hidden state + sets its title. SCUMMRedux: sets title + accent `#FFA200FF`. Then cut OnyxSDK **v0.3.0** and bump both consumers' `GIT_TAG`.

**Tech Stack:** C++20, CMake + Ninja/VS, MSVC. Repos (siblings): `C:\CodingProjects\Personal\OnyxSDK` (engine, branch `main`, tag `v0.2.0`), `C:\CodingProjects\Personal\GoWToolkit` (consumer, branch `feat/onyx-genericization`, consumes OnyxSDK `v0.1.0`), `C:\CodingProjects\Personal\SCUMMRedux` (consumer, branch `main`, consumes OnyxSDK `v0.2.0`).

**Design spec:** `OnyxSDK/docs/superpowers/specs/2026-06-15-onyx-panel-composition-design.md`.

---

## Environment

VS 2022 BuildTools dev shell in the SAME shell call as the build:
```powershell
Import-Module "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" -DevCmdArguments "-arch=x64 -host_arch=x64" -SkipAutomaticLocation
```
Then `Set-Location` to the relevant repo. Build OnyxSDK standalone (it has its own `CMakeLists.txt` + `Examples/` + `Tests/`):
```powershell
Set-Location C:\CodingProjects\Personal\OnyxSDK
cmake -B build ; cmake --build build ; ctest --test-dir build --output-on-failure
```
Consumers build against the **local** OnyxSDK working tree during iteration (no tag needed) via the override:
```
cmake -B build -DFETCHCONTENT_SOURCE_DIR_ONYXSDK=C:/CodingProjects/Personal/OnyxSDK ; cmake --build build ; ctest --test-dir build --output-on-failure
```
Multi-config: if ctest needs a config add `-C Debug`. New `.cpp`/`.h` files need a `cmake -B build` reconfigure (globs). First consumer build linking the engine is heavy (allow up to ~10 min).

**Commit hygiene:** `git add` explicit paths only (never `git add -A`); **no AI-credit trailer**; never commit a red tree. Each repo's commit goes to that repo's current branch (OnyxSDK `main`, GoWToolkit `feat/onyx-genericization`, SCUMMRedux `main`). Use the per-task commit subjects below.

**Verification reality:** the panel-composition outcome is a GUI/UX change — the automated safety net is "each repo builds + its existing tests stay green + SCUMMRedux `--selftest` green". The *visible* result (GoWToolkit panels back; SCUMMRedux shows only Rooms; titles/accent) is confirmed by the user launching the apps. Do NOT attempt to launch GUI windows from a headless agent.

---

## Task 1 (OnyxSDK): Configurable window title

Add a runtime `windowTitle` to `AppConfig` and use it at the two hardcoded sites, replacing `"God Of War Toolkit"`. Default neutral (`"Onyx Toolkit"`). **Runtime-only — do NOT add it to GTKC save/load** (the app sets it each run; persisting it would change the binary config format).

**Files:**
- Modify: `Include/Onyx/Services/AppConfig.h`
- Modify: `Source/App.cpp` (titlebar draw site)
- Modify: `Source/Window/Window.cpp` (glfwCreateWindow site)

- [ ] **Step 1: Add the field to AppConfig**

In `Include/Onyx/Services/AppConfig.h`, add after the window fields (around line 17, after `bool maximized`):
```cpp
  // Window title shown in the (custom) title bar + OS window. Runtime-only:
  // set by the app each run (e.g. in its registrar); NOT serialized to GTKC.
  std::string windowTitle = "Onyx Toolkit";
```
(`<string>` is already included.) **Do not touch `AppConfig::load`/`save`** — leaving `windowTitle` out of them keeps it runtime-only and the on-disk format unchanged.

- [ ] **Step 2: Use it at the titlebar draw site**

In `Source/App.cpp`, find the line (the audit showed it near line 397):
```cpp
m_wantClose = TitleBar::draw(m_window, "God Of War Toolkit", m_decorator.borderless);
```
Replace the literal with the config value:
```cpp
m_wantClose = TitleBar::draw(m_window, m_config->windowTitle.c_str(), m_decorator.borderless);
```
(Confirm `m_config` is non-null here — it is set in `App::init` before the frame loop. If a null-guard is the local style, use `m_config ? m_config->windowTitle.c_str() : "Onyx Toolkit"`.)

- [ ] **Step 3: Use it at the GLFW window-create site**

In `Source/Window/Window.cpp`, find (audit showed ~line 129–130):
```cpp
m_window = glfwCreateWindow(m_config.windowW, m_config.windowH,
                            "God Of War Toolkit", nullptr, nullptr);
```
Replace the literal with `m_config.windowTitle.c_str()`. **Ordering note:** the registrar (where the app sets `windowTitle`) runs during `App::init`, which is AFTER window creation — so at create time `windowTitle` is still the default. The visible custom title bar (Task 1 Step 2, read every frame) updates correctly once the app sets it. To also update the OS/taskbar title, add — at the end of the titlebar draw path or right after the registrar runs — a `glfwSetWindowTitle(m_window, m_config->windowTitle.c_str())` (only when changed, to avoid per-frame churn). Implement whichever keeps the custom titlebar correct; the OS-title sync is a nice-to-have. Grep the repo for any other `"God Of War Toolkit"` / `"God of War Toolkit"` literal and route it through the config too.

- [ ] **Step 4: Build OnyxSDK standalone**

`cmake -B build ; cmake --build build ; ctest --test-dir build --output-on-failure` → green. (No behavior change beyond the default title text.)

- [ ] **Step 5: Commit (OnyxSDK `main`)**
```
git add Include/Onyx/Services/AppConfig.h Source/App.cpp Source/Window/Window.cpp
git commit -m "feat(app): Make window title configurable via AppConfig"
```

---

## Task 2 (OnyxSDK): Promote the 6 opt-in panel headers to public

Move the headers of the panels that will become opt-in to the public include surface so consumers can `#include` + instantiate them. Implementations stay private in `Source/Ui/**`.

The 6 panels + current header → new public header:
| Class | Current header | New public header |
|---|---|---|
| `IsoBrowser` | `Source/Ui/IsoBrowser.h` | `Include/Onyx/App/Panels/IsoBrowser.h` |
| `PakBrowser` | `Source/Ui/PakBrowser.h` | `Include/Onyx/App/Panels/PakBrowser.h` |
| `CameraPanel` | `Source/Ui/CameraPanel.h` | `Include/Onyx/App/Panels/CameraPanel.h` |
| `Onyx::Viewers::AnimCurveView` | `Source/Ui/Viewers/AnimCurveView.h` | `Include/Onyx/App/Panels/AnimCurveView.h` |
| `Onyx::Viewers::Dopesheet` | `Source/Ui/Viewers/Dopesheet.h` | `Include/Onyx/App/Panels/Dopesheet.h` |
| `Onyx::Viewers::WadStatsView` | `Source/Ui/Viewers/WadStatsView.h` | `Include/Onyx/App/Panels/WadStatsView.h` |

**Files:** the 6 headers above (move), their `.cpp` files in `Source/Ui/**` (fix the self-include path), and any engine file that includes them (`Source/App.cpp` etc.).

- [ ] **Step 1: Read each header's includes first**

For each of the 6 headers, read it and note its `#include`s. Each must include only **public** Onyx headers (`<Onyx/App/IPanel.h>`, `<Onyx/Domain/...>`, `<Onyx/Services/...>`, `<Onyx/Viewers/...>`, `<Onyx/Parsers/...>`, `imgui.h`) or other public panel headers. If a header pulls a **private** `Source/**` header, either (a) the dependency is itself promotable (promote it too, into the appropriate public subdir), or (b) refactor so the public header forward-declares and the `.cpp` includes the private detail. Record any such case; resolve it as you move the header.

- [ ] **Step 2: Move the 6 headers**

`git mv` each header to `Include/Onyx/App/Panels/`. Keep the same namespace (`Onyx::App` for IsoBrowser/PakBrowser/CameraPanel; `Onyx::Viewers` for AnimCurveView/Dopesheet/WadStatsView — do NOT change namespaces). Add `#pragma once` if missing. Example:
```
git mv Source/Ui/IsoBrowser.h Include/Onyx/App/Panels/IsoBrowser.h
git mv Source/Ui/PakBrowser.h Include/Onyx/App/Panels/PakBrowser.h
git mv Source/Ui/CameraPanel.h Include/Onyx/App/Panels/CameraPanel.h
git mv Source/Ui/Viewers/AnimCurveView.h Include/Onyx/App/Panels/AnimCurveView.h
git mv Source/Ui/Viewers/Dopesheet.h Include/Onyx/App/Panels/Dopesheet.h
git mv Source/Ui/Viewers/WadStatsView.h Include/Onyx/App/Panels/WadStatsView.h
```

- [ ] **Step 3: Fix include paths**

Update every `#include` of these headers across the engine (their own `.cpp` self-includes in `Source/Ui/**` and `Source/Ui/Viewers/**`, plus `Source/App.cpp` and any other referencing file) to the new `<Onyx/App/Panels/...>` form. Grep for the old paths (`"IsoBrowser.h"`, `Ui/Viewers/Dopesheet.h`, etc.) and the class names to catch every site. Ensure the public `Include/` root is on the engine target's include path (it already is — that's the SDK's public surface).

- [ ] **Step 4: Build OnyxSDK standalone**

`cmake -B build ; cmake --build build ; ctest --test-dir build --output-on-failure` → green. **No behavior change** — the panels are still registered by the engine in this task; only their headers moved. This isolates "did the header move compile" from the registration change in Task 3.

- [ ] **Step 5: Commit (OnyxSDK `main`)**
```
git add Include/Onyx/App/Panels/ Source/Ui/ Source/App.cpp
git commit -m "refactor(app): Promote opt-in generic panels to public Panels/ headers"
```
(Adjust `git add` paths to exactly the moved headers + edited `.cpp`/`.h` files; do not `-A`.)

---

## Task 3 (OnyxSDK): Minimal-core registration

`App::registerPanels()` registers only `StatusBar` + `SettingsWindow`; the 6 opt-in panels are no longer engine-registered (apps add them).

**Files:** `Source/App.cpp` (`registerPanels()`).

- [ ] **Step 1: Reduce the registration list**

In `Source/App.cpp` `App::registerPanels()` (audit: lines 36–67), replace the "Generic (engine) panels" block:
```cpp
  // ── Generic (engine) panels ──
  m_panels.add(std::make_unique<IsoBrowser>());
  m_panels.add(std::make_unique<PakBrowser>());
  m_panels.add(std::make_unique<CameraPanel>());
  m_panels.add(std::make_unique<StatusBar>());
  m_panels.add(std::make_unique<SettingsWindow>());
  m_panels.add(std::make_unique<Onyx::Viewers::AnimCurveView>());
  m_panels.add(std::make_unique<Onyx::Viewers::Dopesheet>());
  m_panels.add(std::make_unique<Onyx::Viewers::WadStatsView>());
```
with the minimal core:
```cpp
  // ── Minimal core (engine) panels — useful for any consumer ──
  // Other generic panels (Iso/Pak Browser, Camera, Anim Curves, Dopesheet,
  // WAD Stats) are now public + opt-in: apps add the ones they want in their
  // registrar (see Onyx/App/Panels/*.h).
  m_panels.add(std::make_unique<StatusBar>());
  m_panels.add(std::make_unique<SettingsWindow>());
```

- [ ] **Step 2: Move the default-hidden logic for moved panels out**

The post-registration visibility block (audit: lines 53–66) `dynamic_cast`s + `find`s for "ISO Browser", "Settings", "Anim Curves", "WAD Stats", "Dopesheet". Keep the `Settings` default-hidden line (Settings is still core). The finds for the 4 now-moved panels (ISO Browser, Anim Curves, WAD Stats, Dopesheet) would return null and become harmless no-ops — **remove those 4 `if` blocks** from the engine (their default-hidden intent moves to GoWToolkit in Task 4). Resulting block keeps only:
```cpp
  if (auto *settings = dynamic_cast<SettingsWindow *>(m_panels.find("Settings")))
    settings->visible = false;
```
Remove now-unused `#include`s of the moved panel headers from `App.cpp` if they're no longer referenced there (they are referenced by the removed `dynamic_cast`s; once removed, drop the includes — but keep any still used, e.g. if `CameraPanel` is referenced elsewhere in App.cpp). Verify by compile.

- [ ] **Step 3: Build OnyxSDK standalone + MinimalViewer**

`cmake -B build ; cmake --build build ; ctest --test-dir build --output-on-failure` → green. Then the example self-test:
```
.\build\<config?>\MinimalViewer.exe --selftest <any small file>
```
(Find the MinimalViewer exe; use any readable file path — its self-test is headless and doesn't depend on the panel set.) Expected exit 0. The engine now registers exactly 2 panels; a manual MinimalViewer launch (optional, user's discretion) would show only Status Bar + Settings.

- [ ] **Step 4: Commit (OnyxSDK `main`)**
```
git add Source/App.cpp
git commit -m "feat(app): Register only minimal core panels (StatusBar + Settings)"
```

---

## Task 4 (GoWToolkit): Re-register the 6 generic panels + set title

Restore GoWToolkit's UX by adding the now-opt-in panels in its registrar, including their default-hidden state, and set its window title. Iterate against the local OnyxSDK working tree.

**Files:** `Apps/GoWToolkit/Source/AppRegistration.cpp` (working dir `C:\CodingProjects\Personal\GoWToolkit`, branch `feat/onyx-genericization`).

- [ ] **Step 1: Add the panels + title to the registrar**

In `Apps/GoWToolkit/Source/AppRegistration.cpp`, add the includes:
```cpp
#include <Onyx/App/Panels/IsoBrowser.h>
#include <Onyx/App/Panels/PakBrowser.h>
#include <Onyx/App/Panels/CameraPanel.h>
#include <Onyx/App/Panels/AnimCurveView.h>
#include <Onyx/App/Panels/Dopesheet.h>
#include <Onyx/App/Panels/WadStatsView.h>
```
Inside the `SetRegistrar` lambda, **before** the existing `WadBrowser`/`Inspector` adds, register the 6 generic panels and set the title + default-hidden state (mirroring the engine's old behavior):
```cpp
    // Generic panels GoWToolkit opts into (previously auto-registered by the
    // engine; now app-composed after Onyx panel-composition change).
    a.addPanel(std::make_unique<Onyx::App::IsoBrowser>());
    a.addPanel(std::make_unique<Onyx::App::PakBrowser>());
    a.addPanel(std::make_unique<Onyx::App::CameraPanel>());
    a.addPanel(std::make_unique<Onyx::Viewers::AnimCurveView>());
    a.addPanel(std::make_unique<Onyx::Viewers::Dopesheet>());
    a.addPanel(std::make_unique<Onyx::Viewers::WadStatsView>());

    if (auto* cfg = a.getConfig()) cfg->windowTitle = "God Of War Toolkit";

    // Default-hidden (matches the engine's prior behavior).
    if (auto* p = dynamic_cast<Onyx::App::IsoBrowser*>(a /* find via registry */ )) ; // see note
```
**Default-hidden note:** the engine used `m_panels.find(name)` after registration. From the registrar, panels were just added to `a` — set their `visible=false` at construction instead, by keeping a local `unique_ptr` to set the flag before `addPanel`, e.g.:
```cpp
    { auto iso = std::make_unique<Onyx::App::IsoBrowser>(); iso->visible = false; a.addPanel(std::move(iso)); }
    { auto ac  = std::make_unique<Onyx::Viewers::AnimCurveView>(); ac->visible = false; a.addPanel(std::move(ac)); }
    { auto ws  = std::make_unique<Onyx::Viewers::WadStatsView>(); ws->visible = false; a.addPanel(std::move(ws)); }
    { auto dp  = std::make_unique<Onyx::Viewers::Dopesheet>(); dp->visible = false; a.addPanel(std::move(dp)); }
    a.addPanel(std::make_unique<Onyx::App::PakBrowser>());   // visible
    a.addPanel(std::make_unique<Onyx::App::CameraPanel>());  // visibility per prior default
```
(`IPanel::visible` is a public member — see `Onyx/App/IPanel.h`. Match the exact prior default-visibility for each panel: previously hidden = ISO Browser, Anim Curves, WAD Stats, Dopesheet; previously visible = PAK Browser, Camera. Settings stays engine-core, still default-hidden by the engine.)

- [ ] **Step 2: Build GoWToolkit against local OnyxSDK**

```
Set-Location C:\CodingProjects\Personal\GoWToolkit
cmake -B build -DFETCHCONTENT_SOURCE_DIR_ONYXSDK=C:/CodingProjects/Personal/OnyxSDK ; cmake --build build ; ctest --test-dir build --output-on-failure
```
Expected: green, including golden GOW2/GOWR. (If GoWToolkit currently pins OnyxSDK `v0.1.0`, the override replaces the fetched source with the local tree regardless of tag, so the new panel headers are available.) A manual GoWToolkit launch (user's discretion) should show the same panels as before.

- [ ] **Step 3: Commit (GoWToolkit `feat/onyx-genericization`)**
```
git add Apps/GoWToolkit/Source/AppRegistration.cpp
git commit -m "feat(app): Register generic panels app-side after Onyx panel-composition"
```

---

## Task 5 (SCUMMRedux): Set title + accent

Give SCUMMRedux its title and the `#FFA200` accent. It already gets the clean panel slate (only Rooms + core) for free.

**Files:** `Source/Gui/ScummGuiApp.cpp` (working dir `C:\CodingProjects\Personal\SCUMMRedux`, branch `main`).

- [ ] **Step 1: Set title + accent in the registrar**

In `Source/Gui/ScummGuiApp.cpp` `InstallScummPanels`, inside the `SetRegistrar` lambda (after `addPanel(ScummTreePanel)`), set the config:
```cpp
        if (auto* cfg = a.getConfig()) {
            cfg->windowTitle = "SCUMMRedux";
            cfg->accentR = 1.0f;    // #FF
            cfg->accentG = 0.635f;  // #A2 (162/255)
            cfg->accentB = 0.0f;    // #00
            cfg->accentA = 1.0f;
        }
```
(Confirm `Onyx::Services::AppConfig` is reachable via `a.getConfig()` — it is, per `App::getConfig()`. The accent is applied at window init by `ThemeManager`; if the accent doesn't take effect because theme is applied before the registrar runs, additionally call `Onyx::Theme::ApplyTheme(ImVec4(1.0f,0.635f,0.0f,1.0f), (Onyx::Theme::ThemeMode)cfg->themeMode)` here — verify the include/namespace from `Onyx/Services/ThemeManager.h`.)

- [ ] **Step 2: Build SCUMMRedux against local OnyxSDK + self-test**

```
Set-Location C:\CodingProjects\Personal\SCUMMRedux
cmake -B build -DFETCHCONTENT_SOURCE_DIR_ONYXSDK=C:/CodingProjects/Personal/OnyxSDK ; cmake --build build ; ctest --test-dir build --output-on-failure
.\build\<config?>\scummredux-gui.exe --selftest "C:\CodingProjects\Personal\The Curse of Monkey Island\ScummVM\monkey3\COMI.LA0"
```
Expected: ctest green; `--selftest` exit 0 ("95 rooms, room 1 decoded 640x480 RGBA"). The panel slate / title / accent are confirmed by the user's manual launch.

- [ ] **Step 3: Commit (SCUMMRedux `main`)**
```
git add Source/Gui/ScummGuiApp.cpp
git commit -m "feat(gui): Set SCUMMRedux window title and accent color"
```

---

## Task 6: Release OnyxSDK v0.3.0 + bump consumers

Cut the engine release and point both consumers at it.

- [ ] **Step 1: Verify OnyxSDK is release-ready**

On `C:\CodingProjects\Personal\OnyxSDK` (`main`): `cmake -B build ; cmake --build build ; ctest --test-dir build --output-on-failure` → green; MinimalViewer `--selftest` green. Confirm the three OnyxSDK commits (Tasks 1–3) are on `main` and pushed (`git push origin main`).

- [ ] **Step 2: Tag + push v0.3.0**

Confirm OnyxSDK's release mechanism (annotated tag; verify whether a CI release workflow exists like GoWToolkit's — if so follow it, else a plain annotated tag is the release):
```
git tag -a v0.3.0 -m "v0.3.0 — app-composable panels + configurable window title"
git push origin v0.3.0
```

- [ ] **Step 3: Bump SCUMMRedux to v0.3.0**

In `C:\CodingProjects\Personal\SCUMMRedux\CMakeLists.txt`, change the OnyxSDK `FetchContent_Declare(... GIT_TAG v0.2.0)` to `GIT_TAG v0.3.0`. Reconfigure WITHOUT the local override (to prove the tag works):
```
Set-Location C:\CodingProjects\Personal\SCUMMRedux
cmake -B build ; cmake --build build ; ctest --test-dir build --output-on-failure
```
→ green. Commit:
```
git add CMakeLists.txt
git commit -m "build(deps): Bump OnyxSDK to v0.3.0 (panel composition)"
```

- [ ] **Step 4: Bump GoWToolkit to v0.3.0**

In `C:\CodingProjects\Personal\GoWToolkit` root `CMakeLists.txt`, change the OnyxSDK `GIT_TAG` (currently `v0.1.0`) to `v0.3.0`. Reconfigure without the override:
```
Set-Location C:\CodingProjects\Personal\GoWToolkit
cmake -B build ; cmake --build build ; ctest --test-dir build --output-on-failure
```
→ green (golden GOW2/GOWR). Commit:
```
git add CMakeLists.txt
git commit -m "build(deps): Bump OnyxSDK to v0.3.0 (panel composition)"
```
(If the GoWToolkit bump from v0.1.0 → v0.3.0 surfaces an unrelated breakage from the intervening v0.2.0 container primitives, report it — those are additive and should not break GoWToolkit, but verify.)

---

## Verificação final

- [ ] **Step 1: OnyxSDK** builds standalone, tests + MinimalViewer `--selftest` green; engine registers only StatusBar + Settings; the 6 panel headers are public under `Include/Onyx/App/Panels/`; `v0.3.0` tagged + pushed.
- [ ] **Step 2: GoWToolkit** builds against `v0.3.0`, golden GOW2/GOWR green. **User manual check:** launch shows the same panels as before and title "God Of War Toolkit". (No regression.)
- [ ] **Step 3: SCUMMRedux** builds against `v0.3.0`, `--selftest` green. **User manual check:** launch shows ONLY "Rooms" + Status Bar + Settings (no ISO/PAK/Camera/AnimCurves/Dopesheet), title "SCUMMRedux", orange `#FFA200` accent.

---

## Roadmap (follow-on, unchanged)

- **Profile-driven Open dialog** (`App::drawOpenDialog` hardcoded GoW game list) — deferred goal-#2 item.
- **SCUMMRedux double-click perf/freeze** — the next in-repo pass (cache the `.LAx` buffer once per disk; decode off the UI thread via `TaskManager`).
- Promote a generic `AssetEntry` tree-browser into Onyx if `ScummTreePanel` ↔ `WadBrowser` converge.

## Self-review notes (coverage vs spec)

- Spec "minimal core = StatusBar + Settings" → Task 3. ✓
- Spec "6 generic panels public + opt-in" → Task 2 (promote headers) + Task 4 (GoWToolkit opts in). ✓
- Spec "configurable window title (AppConfig field, 2 sites, runtime-only)" → Task 1. ✓
- Spec "accent app-settable (#FFA200FF)" → Task 5 (no engine change). ✓
- Spec "GoWToolkit no regression (re-register + title + default-hidden)" → Task 4. ✓
- Spec "sequencing: OnyxSDK → release v0.3.0 → consumers bump" → Tasks 1–3 then 6; consumers iterate via local override (Tasks 4–5) then bump (Task 6). ✓
- Spec testing (OnyxSDK tests + MinimalViewer; GoWToolkit golden; SCUMMRedux --selftest; manual GUI) → per-task + final verification. ✓
- Open questions: header-promotion cleanliness (Task 2 Step 1), title live-update (Task 1 Step 3), consumer pins (Task 6) — addressed inline. Panel-count automated assertion: not feasible headlessly (App needs a GL context) → verified by MinimalViewer + manual launches (noted).
```
