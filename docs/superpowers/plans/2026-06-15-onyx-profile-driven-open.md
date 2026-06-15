# Onyx Profile-Driven Open Dialog — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make File→Open profile-driven: a native file picker filtered to the union of registered profiles' supported extensions, auto-detecting the profile (no GoW dropdown, no hardcoded filter, no engine→panel coupling). Adding a game = registering a profile with a filter + Detect.

**Architecture:** OnyxSDK: add `Onyx::Domain::OpenFilter` + `IAssetProfile::GetOpenFilter()`; give `SystemOpenFileDialog` a filter parameter; replace `App::drawOpenDialog`/recents with a native-picker→`LoadWad(path)` flow and remove the `PakBrowser`/`WadBrowser` coupling. Consumers implement `GetOpenFilter()` on their profiles; GoWToolkit re-adds browser auto-show via an event subscription. Release v0.5.0 + bump consumers.

**Tech Stack:** C++20, CMake + Ninja/VS, MSVC, ImGui, Win32/Cocoa file dialogs. Repos: `C:\CodingProjects\Personal\OnyxSDK` (branch `main`, tag `v0.4.0`), `C:\CodingProjects\Personal\GoWToolkit` (branch `feat/onyx-genericization`, consumes v0.4.0), `C:\CodingProjects\Personal\SCUMMRedux` (branch `main`, consumes v0.4.0).

**Design spec:** `OnyxSDK/docs/superpowers/specs/2026-06-15-onyx-profile-driven-open-design.md`.

**Audited sites (verify by reading before editing):** `Source/App.cpp` `drawOpenDialog` ~:215-302, `openRecentFile` ~:304-323, `drawPopups` :329; `Include/Onyx/App/App.h` (`m_showOpenDialog`, `m_openDialogSelectedGame`, the menu wiring). `Source/Ui/Platform/SystemFileDialog.cpp` (`SystemOpenFileDialog()` hardcoded filter) + its decl in `Include/Onyx/App/UIHelpers.h`. `Source/Core/AssetDatabase.cpp` `LoadWad` (ISO redirect + `DetectProfileForFile` on empty hint). `Include/Onyx/Domain/IAssetProfile.h`, `Include/Onyx/Services/ProfileManager.h` (`GetProfiles()`).

---

## Environment

VS 2022 BuildTools dev shell, SAME shell call as the build; `Set-Location` per repo:
```powershell
Import-Module "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" -DevCmdArguments "-arch=x64 -host_arch=x64" -SkipAutomaticLocation
```
- OnyxSDK standalone: `Set-Location C:\CodingProjects\Personal\OnyxSDK ; cmake -B build ; cmake --build build ; ctest --test-dir build --output-on-failure` + `MinimalViewer --selftest <file>`.
- Consumers iterate against local engine: `cmake -B build -DFETCHCONTENT_SOURCE_DIR_ONYXSDK=C:/CodingProjects/Personal/OnyxSDK ; cmake --build build ; ctest --test-dir build --output-on-failure`.
- New files need `cmake -B build` reconfigure; ctest config `-C Debug` if needed; first engine-linked consumer build ~10 min.

**Commit hygiene:** explicit `git add` paths only; **no AI-credit trailer**; never commit red; per-repo branches. **GoWToolkit has pre-existing unrelated dirty files (third_party + untracked fixture) — never stage them.**

**Verification reality:** the open flow is GUI — automated net = builds + tests + MinimalViewer `--selftest`; the visible result (File→Open lists the right groups, opens files, no GoW dropdown) is the user's manual check. **Do NOT launch GUI windows headlessly.**

**STOP-before-release:** Implement **Tasks 1–5 only**; STOP before Task 6 (release) — it runs after the user's manual GUI acceptance.

---

## Task 1 (OnyxSDK): `OpenFilter` + `IAssetProfile::GetOpenFilter()`

**Files:** `Include/Onyx/Domain/IAssetProfile.h`.

- [ ] **Step 1: Add the struct + virtual**

In `Include/Onyx/Domain/IAssetProfile.h`, inside `namespace Onyx::Domain`, add above `class IAssetProfile` (ensure `<string>` + `<vector>` are included — they are):
```cpp
// Declares the files a profile can open, for the File->Open picker.
// extensions are lowercase, WITHOUT the leading dot (e.g. {"iso","wad"}).
struct OpenFilter {
    std::string label;                  // e.g. "God of War II (PS2)"
    std::vector<std::string> extensions;
    bool valid() const { return !extensions.empty(); }
};
```
And add the virtual to `IAssetProfile` (default empty ⇒ not openable from the GUI):
```cpp
    // Files this profile can open (for the File->Open picker). Default: none.
    virtual OpenFilter GetOpenFilter() const { return {}; }
```

- [ ] **Step 2: Build** — `cmake --build build` (header-only change; engine + tests still compile). Commit.
```
git add Include/Onyx/Domain/IAssetProfile.h
git commit -m "feat(domain): Add IAssetProfile::GetOpenFilter for profile-driven open"
```

---

## Task 2 (OnyxSDK): `SystemOpenFileDialog` takes filters

**Files:** `Include/Onyx/App/UIHelpers.h` (decl), `Source/Ui/Platform/SystemFileDialog.cpp` (impl), `Source/Ui/Platform/*.mm` (macOS, if the Cocoa shim needs the union — check).

- [ ] **Step 1: Change the declaration**

In `Include/Onyx/App/UIHelpers.h`, change `std::string SystemOpenFileDialog();` to:
```cpp
#include <Onyx/Domain/IAssetProfile.h>   // OpenFilter
#include <vector>
// Native open dialog filtered to the given groups (plus an auto-added
// "All Supported Files" union group and an "All Files" group). Empty list ⇒ All Files.
std::string SystemOpenFileDialog(const std::vector<Onyx::Domain::OpenFilter>& filters);
```
(Keep `SystemSaveFileDialog` as-is.)

- [ ] **Step 2: Rewrite the Windows + macOS impl**

In `Source/Ui/Platform/SystemFileDialog.cpp`, replace the hardcoded `SystemOpenFileDialog()` body. Build the filter dynamically:
- **Union** of all `filters[].extensions` → an "All Supported Files" group `*.ext1;*.ext2;...`.
- One group per `filters[i]` → `"<label> (*.e1, *.e2)\0*.e1;*.e2\0"`.
- Trailing `"All Files (*.*)\0*.*\0"`.
Windows: compose the `\0`-delimited buffer into a `std::string` (use `'\0'` separators + a final `'\0'`); set `ofn.lpstrFilter = buf.data()`. macOS: pass the flat union extension array to `macos_openFileDialog(exts, count)` (per Open Question #1 — macOS filters by extension union; per-group labels are a Windows nicety). If `filters` is empty, fall back to "All Files".
Concrete Windows skeleton:
```cpp
std::string SystemOpenFileDialog(const std::vector<Onyx::Domain::OpenFilter>& filters) {
#ifdef _WIN32
    std::vector<std::string> allExts;
    for (auto& f : filters) for (auto& e : f.extensions) allExts.push_back(e);
    std::string buf;
    auto group = [&](const std::string& label, const std::vector<std::string>& exts){
        std::string pat; for (size_t i=0;i<exts.size();++i){ if(i) pat += ';'; pat += "*." + exts[i]; }
        buf += label; buf.push_back('\0'); buf += pat; buf.push_back('\0');
    };
    if (!allExts.empty()) group("All Supported Files", allExts);
    for (auto& f : filters) if (f.valid()) group(f.label, f.extensions);
    group("All Files (*.*)", {"*"});            // yields "*.*"? ensure pattern is "*.*"
    buf.push_back('\0');
    OPENFILENAMEA ofn = {}; char path[MAX_PATH] = {};
    ofn.lStructSize = sizeof(ofn); ofn.lpstrFilter = buf.c_str();
    ofn.lpstrFile = path; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) return std::string(path);
    return "";
#elif defined(__APPLE__)
    std::vector<const char*> exts; for (auto& f: filters) for (auto& e: f.extensions) exts.push_back(e.c_str());
    const char* result = macos_openFileDialog(exts.empty()?nullptr:exts.data(), (int)exts.size());
    if (result) { std::string p(result); free(const_cast<char*>(result)); return p; }
    return "";
#else
    return "";
#endif
}
```
Fix the "All Files" pattern so it's literally `*.*` (adjust the `group` call/`pat` for the wildcard case). Include `<Onyx/Domain/IAssetProfile.h>`.

- [ ] **Step 3: Build OnyxSDK + commit**

`cmake --build build ; ctest --test-dir build --output-on-failure` → green (this breaks the old no-arg callers — App.cpp is updated in Task 3; if App.cpp still calls the old signature, Task 3 fixes it; build the lib target if App.cpp lags, or do Task 3 before building the full app). Commit after Task 3 builds clean, OR commit the dialog change together with Task 3 since they're coupled. **Recommendation:** fold Task 2's commit into Task 3 (the only caller is App.cpp) to avoid a transient red tree. If committing alone, ensure all call sites are updated first.
```
git add Include/Onyx/App/UIHelpers.h Source/Ui/Platform/SystemFileDialog.cpp
git commit -m "feat(ui): SystemOpenFileDialog takes profile-built filters"
```

---

## Task 3 (OnyxSDK): Profile-driven File→Open flow

**Files:** `Source/App.cpp`, `Include/Onyx/App/App.h`.

- [ ] **Step 1: Replace the open trigger state**

In `Include/Onyx/App/App.h`, replace `bool m_showOpenDialog` (and remove `int m_openDialogSelectedGame`) with `bool m_requestOpenFile = false;`. Remove the `drawOpenDialog` private declaration (its body is deleted below).

- [ ] **Step 2: Implement the new open flow**

In `Source/App.cpp`:
- Delete the `drawOpenDialog()` body (the `gameVersions[]` popup + the OK switch). 
- Add a private helper `void App::handleOpenFileRequest()`:
```cpp
void App::handleOpenFileRequest() {
    if (!m_requestOpenFile) return;
    m_requestOpenFile = false;
    std::vector<Onyx::Domain::OpenFilter> filters;
    for (auto& p : Onyx::Services::ProfileManager::Get().GetProfiles()) {
        auto f = p->GetOpenFilter();
        if (f.valid()) filters.push_back(std::move(f));
    }
    std::string path = SystemOpenFileDialog(filters);
    if (path.empty()) return;
    if (m_db.LoadWad(path)) {                       // empty hint → detect; ISO handled inside
        m_recentFiles.Add(path, "", "");            // dispatch is re-detect on reopen; label optional
    } else {
        LOG_WARN("[App] Unsupported or unreadable file: %s", path.c_str());
        // surface via the existing status channel (StatusBar/log) — see Open Question #2
    }
}
```
- Call `handleOpenFileRequest()` once per frame from `drawPopups()` (replace the `drawOpenDialog()` call there): `void App::drawPopups() { handleOpenFileRequest(); }`.
- The File→Open menu item (find it in `drawMenuItems`) sets `m_requestOpenFile = true;` instead of `m_showOpenDialog = true;`.
- **Remove the GoW panel coupling:** delete every `dynamic_cast<PakBrowser*>(...)`, `m_panels.find("WAD Browser"|"PAK Browser")`, and `ImGui::SetWindowFocus("PAK Browser"|"WAD Browser")` from the open + recent paths. Remove the now-unused `#include <Onyx/App/Panels/PakBrowser.h>` from App.cpp (grep to confirm no other use).

- [ ] **Step 3: Simplify `openRecentFile`**

Replace the ISO-vs-WAD branch + panel auto-show with:
```cpp
void App::openRecentFile(Onyx::Services::RecentEntry entry) {
    if (!fs::exists(entry.path)) return;
    if (m_db.LoadWad(entry.path))                    // re-detect; ISO handled inside
        m_recentFiles.Add(entry.path, entry.gameHint, entry.fileType);  // bump
}
```
(Keep the `RecentEntry` fields for display; dispatch is `LoadWad` re-detect.)

- [ ] **Step 4: Build OnyxSDK + MinimalViewer**

`cmake -B build ; cmake --build build ; ctest --test-dir build --output-on-failure` → green; `MinimalViewer --selftest <file>` → exit 0. (MinimalViewer has no profiles with filters → File→Open would show only "All Files"; that's fine — it has no open menu use in selftest.)

- [ ] **Step 5: Commit (OnyxSDK `main`)** — include Task 2's files here if not already committed:
```
git add Include/Onyx/App/App.h Source/App.cpp Include/Onyx/App/UIHelpers.h Source/Ui/Platform/SystemFileDialog.cpp
git commit -m "feat(app): Profile-driven File->Open (native picker + auto-detect)"
```

---

## Task 4 (GoWToolkit): Profile filters + browser auto-show via event

**Files:** `Apps/GoWToolkit/Source/core/profiles/gow2/ProfileGOW2.{h,cpp}`, `.../gowr/ProfileGOWR.{h,cpp}`, `Apps/GoWToolkit/Source/AppRegistration.cpp`.

- [ ] **Step 1: Add `GetOpenFilter` overrides**

Read each profile header; add the override:
- `ProfileGOW2`: `Onyx::Domain::OpenFilter GetOpenFilter() const override { return {"God of War I / II (PS2)", {"iso","wad","pak"}}; }`
- `ProfileGOWR`: `... { return {"God of War (2018) / Ragnarok", {"wad","pak"}}; }`
(Match the exact extensions these profiles' `Detect` accepts — read `Detect` and mirror it; adjust if they accept different exts.)

- [ ] **Step 2: Re-add browser auto-show via event**

In `AppRegistration.cpp`'s registrar lambda, subscribe to restore today's UX (browser shown + focused on open):
```cpp
    EventWadOpened::subscribe([&a](Onyx::Domain::AssetContainer*) {
        if (auto* p = a.findPanel("WAD Browser")) p->visible = true;   // use the real panel-find API
    });
    // (and EventPakOpened for the PAK Browser, if that event exists)
```
(Use the actual panel-lookup API available to the registrar — verify how a registrar can reach panels; if there's no public find, set the panel visible at construction or via the panel's own event subscription. Confirm `EventWadOpened`/`EventPakOpened` signatures in `Onyx/Services/Events.h`.) Keep the existing layout/title/panel registrations.

- [ ] **Step 3: Build against local OnyxSDK + verify**
```
Set-Location C:\CodingProjects\Personal\GoWToolkit
cmake -B build -DFETCHCONTENT_SOURCE_DIR_ONYXSDK=C:/CodingProjects/Personal/OnyxSDK ; cmake --build build ; ctest --test-dir build --output-on-failure
```
→ golden GOW2/GOWR green. User manual check (later): File→Open shows .iso/.wad/.pak groups; opening a GoW file loads it and the browser appears.

- [ ] **Step 4: Commit (GoWToolkit `feat/onyx-genericization`)**
```
git add Apps/GoWToolkit/Source/core/profiles/gow2/ProfileGOW2.h Apps/GoWToolkit/Source/core/profiles/gow2/ProfileGOW2.cpp Apps/GoWToolkit/Source/core/profiles/gowr/ProfileGOWR.h Apps/GoWToolkit/Source/core/profiles/gowr/ProfileGOWR.cpp Apps/GoWToolkit/Source/AppRegistration.cpp
git commit -m "feat(app): Declare GetOpenFilter + auto-show browser on open via event"
```
(Adjust the exact profile file paths to what exists; add only those files.)

---

## Task 5 (SCUMMRedux): ProfileScumm filter

**Files:** `Source/Gui/ProfileScumm.h` (branch `main`).

- [ ] **Step 1: Add the override**

In `Source/Gui/ProfileScumm.h`, add to the class:
```cpp
    Onyx::Domain::OpenFilter GetOpenFilter() const override { return {"SCUMM (CMI / v8)", {"la0"}}; }
```
(`Detect` already matches `.LA0`.)

- [ ] **Step 2: Build against local OnyxSDK + verify**
```
Set-Location C:\CodingProjects\Personal\SCUMMRedux
cmake -B build -DFETCHCONTENT_SOURCE_DIR_ONYXSDK=C:/CodingProjects/Personal/OnyxSDK ; cmake --build build ; ctest --test-dir build --output-on-failure
.\build\<config?>\scummredux-gui.exe --selftest "C:\CodingProjects\Personal\The Curse of Monkey Island\ScummVM\monkey3\COMI.LA0"
```
→ ctest green; `--selftest` exit 0. User manual check (later): File→Open shows a SCUMM (*.la0) group, opening COMI.LA0 populates Rooms; no GoW games listed.

- [ ] **Step 3: Commit (SCUMMRedux `main`)**
```
git add Source/Gui/ProfileScumm.h
git commit -m "feat(gui): Declare ProfileScumm open filter (*.la0)"
```

---

## Task 6 (after user acceptance): Release v0.5.0 + bump consumers

**Do NOT start until the user manually accepts (File→Open works in both apps).** Pushes a public release.

- [ ] **Step 1:** OnyxSDK `main` green + MinimalViewer `--selftest` ok → `git push origin main` → `git tag -a v0.5.0 -m "v0.5.0 - Profile-driven File->Open (no hardcoded games/filters)"` → `git push origin v0.5.0`.
- [ ] **Step 2:** SCUMMRedux `CMakeLists.txt` `GIT_TAG v0.4.0 → v0.5.0`; fresh override-less build (`Remove-Item -Recurse -Force build; cmake -B build; cmake --build build; ctest ...`) + `--selftest` → green; commit `build(deps): Bump OnyxSDK to v0.5.0 (profile-driven open)`.
- [ ] **Step 3:** GoWToolkit `CMakeLists.txt` `GIT_TAG → v0.5.0`; fresh override-less build → golden green; commit (only `CMakeLists.txt`) `build(deps): Bump OnyxSDK to v0.5.0 (profile-driven open)`.

---

## Verificação final

- [ ] **Step 1:** OnyxSDK builds standalone, tests + MinimalViewer `--selftest` green; no `gameVersions[]`/`m_openDialogSelectedGame`/`drawOpenDialog` left; no panel names in the open/recent paths; `SystemOpenFileDialog` takes filters.
- [ ] **Step 2 (user):** GoWToolkit — File→Open lists .iso/.wad/.pak (no fixed dropdown), opens a GoW file, browser appears; golden tests green.
- [ ] **Step 3 (user):** SCUMMRedux — File→Open lists *.la0 (no GoW games), opens COMI.LA0 → Rooms populate.

---

## Roadmap (next specs)
- **Spec 2 — Treeview** (resource tree by category + filters; maybe a generic Onyx browser).
- **Spec 3 — Informer / Markdown viewer** (engine-level Onyx docs viewer).
- Deferred: the dockspace ID `"GoWToolDockSpace"` rename; Phase B config (binary→text/extensible).

## Self-review notes (coverage vs spec)
- Spec "`IAssetProfile::GetOpenFilter()` + `OpenFilter`" → Task 1. ✓
- Spec "`SystemOpenFileDialog` takes filters; drop hardcoded `*.wad;*.iso;*.pak`" → Task 2. ✓
- Spec "native picker + auto-detect, no dropdown; `LoadWad(path)`; no-match message; recents re-detect" → Task 3. ✓
- Spec "remove GoW panel coupling; consumers react via events" → Task 3 (remove) + Task 4 (GoWToolkit event). ✓
- Spec "consumers implement GetOpenFilter" → Tasks 4 (GoW2/GoWR) + 5 (Scumm). ✓
- Spec "release v0.5.0 + bumps; stop before release for acceptance" → Task 6 (gated). ✓
- Open questions (macOS union filter; no-match channel; recents fields; OpenFilter location in Domain) → addressed in Tasks 2/3/1. ✓
