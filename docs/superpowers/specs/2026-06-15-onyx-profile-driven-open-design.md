# Onyx Profile-Driven Open Dialog — Design

> First of three specs (Open → Treeview → Informer/Markdown) addressing SCUMMRedux's browsing UX. This one de-hardcodes OnyxSDK's File→Open: today it shows a GoW game dropdown and a fixed `*.wad;*.iso;*.pak` filter, so a non-GoW consumer (SCUMMRedux) can't open its files from the GUI. After this, **File→Open shows a native file picker filtered to whatever the registered profiles support, and auto-detects the profile** — adding a game is just registering a profile.

## Context & goals

OnyxSDK's open flow is GoW-shaped in three hardcoded places: (1) `App::drawOpenDialog` shows a fixed `gameVersions[]` dropdown and branches on the choice; (2) `SystemOpenFileDialog()` hardcodes the `*.wad;*.iso;*.pak` filter; (3) the open/recent paths reference the GoW-specific `PakBrowser`/`WadBrowser` panels to auto-show/focus them. SCUMMRedux exposed (1); (2) and (3) are pre-existing goal-#2 carry-forwards. This spec makes the open flow **profile-driven** so every consumer's supported files are openable with zero engine edits — advancing goal #2 (Onyx as a generic base) and resolving all three carry-forwards at once.

## Founding decisions (user-approved)

1. **Native filtered picker + auto-detect, no game dropdown.** File→Open opens the OS-native file dialog filtered to the union of registered profiles' extensions; the chosen file's profile is auto-detected (`ProfileManager::DetectProfileForFile`).
2. **Profiles declare their openable files** via a new `IAssetProfile::GetOpenFilter()`.
3. **Engine stays generic** — no panel names in the open path; consumers wire their own post-open behavior via events.

## Current state (audited — exact sites)

- `Source/App.cpp` `drawOpenDialog` (~:215-302): in-app ImGui popup with hardcoded `const char* gameVersions[] = {"God of War 1","God of War 2","God of War (2018)","God of War Ragnarok"}` + `m_openDialogSelectedGame`; on OK calls `SystemOpenFileDialog()` then `switch(m_openDialogSelectedGame)` → `m_db.LoadIsoPakAsync` / `m_db.LoadWadAsync(p, hint)` with hints `"gow1"/"gow2"/"ragnarok"`, plus `dynamic_cast<PakBrowser*>(m_panels.find("PAK Browser"))->visible=true` / `SetWindowFocus("PAK Browser"|"WAD Browser")` and `m_recentFiles.Add(path, hint, "ISO"|"WAD")`.
- `App::openRecentFile` (~:304-323): mirrors the above by recorded `fileType`/`gameHint` + the same PakBrowser/WadBrowser auto-show.
- `Source/Ui/Platform/SystemFileDialog.cpp`: `std::string SystemOpenFileDialog()` (no params) hardcodes the Windows `lpstrFilter = "Supported Files (*.wad,*.iso,*.pak)\0*.wad;*.iso;*.pak\0..."` and the macOS `exts[]={"wad","iso","pak"}`.
- `Source/Core/AssetDatabase.cpp` `LoadWad(path, gameHint="")` (~:130): redirects ISOs to the PAK path internally and, when `gameHint` is empty, resolves the profile via `ProfileManager::DetectProfileForFile(path)`. So `LoadWad(path)` already does ISO-vs-file + profile auto-detect.
- `Include/Onyx/Domain/IAssetProfile.h`: has `GetName()`, `Detect(path)`, `MountArchive`, `ParseContainer`, `LoadFromArchive`. `Include/Onyx/Services/ProfileManager.h`: `GetProfiles()`, `DetectProfileForFile(path)`, `FindProfileByHint(hint)`.

## Design

### OnyxSDK (engine)

1. **`IAssetProfile::GetOpenFilter()`** — new virtual with a default empty return:
   ```cpp
   struct OpenFilter { std::string label; std::vector<std::string> extensions; }; // extensions WITHOUT dots, lowercase
   virtual OpenFilter GetOpenFilter() const { return {}; }   // empty ⇒ profile contributes no openable files
   ```
   Lives in `Onyx::Domain` next to `IAssetProfile`. A profile that returns a non-empty filter becomes openable from the GUI.

2. **`SystemOpenFileDialog` takes filters** — change the signature to accept a portable, structured filter list so it works on Win + macOS:
   ```cpp
   std::string SystemOpenFileDialog(const std::vector<Onyx::Domain::OpenFilter>& filters);
   ```
   It builds an "All Supported Files" group (union of all extensions) first, then one group per filter, then "All Files (*.*)". On Windows it composes the `\0`-delimited `lpstrFilter`; on macOS it passes the union extension array to `macos_openFileDialog`. Remove the hardcoded `*.wad;*.iso;*.pak`. (Resolves the M4 "injectable filter" carry-forward.)

3. **Engine File→Open** — replace `drawOpenDialog` (and the `gameVersions[]`/`m_openDialogSelectedGame` state) with a direct flow: the File→Open menu action gathers `OpenFilter`s from `ProfileManager::Get().GetProfiles()` (skip empties), calls `SystemOpenFileDialog(filters)`, and on a non-empty path calls `m_db.LoadWad(path)` (empty hint → auto-detect; ISO handled inside). If no profile matches (`DetectProfileForFile` is null), surface a clear "Unsupported file" message (log + a status/toast), don't crash. Add to recents the path + the detected profile's `GetName()`.

4. **Recents** — `openRecentFile` becomes `m_db.LoadWad(entry.path)` (re-detect on open); drop the ISO-vs-WAD branch and the gameHint/fileType reliance for dispatch (keep storing a label for display only). 

5. **Remove the GoW panel coupling** — delete the `dynamic_cast<PakBrowser*>` / `m_panels.find("WAD Browser")` / `SetWindowFocus(...)` calls from the open + recent paths. The engine no longer names any panel. Consumers that want a panel shown/focused on open subscribe to `EventWadOpened` (and/or `EventPakOpened`) in their registrar. (Resolves the PakBrowser-coupling carry-forward; the App.cpp `#include` of `PakBrowser.h` from the panel-composition pass can then go too.)

### Consumers

- **GoWToolkit:** `ProfileGOW2::GetOpenFilter` → `{"God of War I / II (PS2)", {"iso","wad","pak"}}`; `ProfileGOWR::GetOpenFilter` → `{"God of War (2018) / Ragnarök", {"wad","pak"}}`. In its registrar, optionally subscribe to `EventWadOpened`/`EventPakOpened` to auto-show + focus its WadBrowser/PakBrowser (restoring today's behavior).
- **SCUMMRedux:** `ProfileScumm::GetOpenFilter` → `{"SCUMM (CMI / v8)", {"la0"}}`. The "Rooms" panel is always visible, so no auto-show needed; File→Open now lists `.la0` and opens `COMI.LA0` → tree populates (in addition to the existing launch-path open).

## Data flow

File→Open → engine collects `OpenFilter`s from registered profiles → native picker (All Supported + per-profile + All Files) → user picks a path → `AssetDatabase::LoadWad(path)` (detect profile; ISO→PAK internally) → on success `EventWadOpened`/`EventPakOpened` fires (consumers react: show/focus their browser) + recents updated; on no-match → "Unsupported file" message. Adding a new game/format = register a profile whose `GetOpenFilter` + `Detect` cover its files; no engine edits.

## Testing

- **OnyxSDK:** builds standalone; headless tests + MinimalViewer `--selftest` green. (The open flow is GUI; the testable seam is `GetOpenFilter` aggregation — consider a small unit if a pure aggregator function is extracted.)
- **GoWToolkit:** builds against local OnyxSDK; golden GOW2/GOWR green. **User manual check:** File→Open shows .iso/.wad/.pak groups, opening a GoW .iso/.wad loads it and the browser appears/focuses as before (via the event subscription).
- **SCUMMRedux:** `--selftest` green. **User manual check:** File→Open shows a SCUMM (*.la0) group, opening `COMI.LA0` populates the Rooms tree; no GoW games appear in the dialog.

## Sequencing & release

Same dance: OnyxSDK changes on `main`; iterate consumers via `-DFETCHCONTENT_SOURCE_DIR_ONYXSDK=...`; cut OnyxSDK **v0.5.0**; bump both consumers' `GIT_TAG` v0.4.0 → v0.5.0 and rebuild without override. Implement the engine + consumer changes, verify via local override, stop before the release for the user's manual GUI acceptance, then tag + bump.

## Out of scope (next specs)

- **Spec 2 — Treeview:** the full resource tree organized by category (per the ScummRev ViewerManager category model) with **filters**; replaces/extends the current Rooms-only panel; drives viewer-open by category. May promote a generic `AssetEntry` tree-browser into Onyx.
- **Spec 3 — Informer / Markdown viewer:** an engine-level `Onyx::Viewers` Markdown/HTML viewer (the ScummRev Informer → in-tool docs), benefiting every consumer.

## Open questions / to verify in the plan

1. **macOS filter plumbing:** `macos_openFileDialog(extensions, count)` takes a flat extension array — confirm passing the union is acceptable (per-group labels are a Windows nicety; macOS may only filter by extension union). Keep the cross-platform signature carrying structured filters; flatten for macOS.
2. **No-match UX:** is there an existing toast/status mechanism, or use the StatusBar/log? (Pick the simplest existing channel for the "Unsupported file" message.)
3. **Recents struct:** `RecentEntry{path, gameHint, fileType}` — keep the fields (store profile `GetName()` in `gameHint`, `""`/label in `fileType`) for display, but dispatch purely via `LoadWad(path)` re-detect. Confirm no other consumer reads those fields for logic.
4. **`OpenFilter` location:** in `IAssetProfile.h` (Domain) so both the profile interface and `SystemOpenFileDialog`/App can see it without a cycle. Confirm include direction.
