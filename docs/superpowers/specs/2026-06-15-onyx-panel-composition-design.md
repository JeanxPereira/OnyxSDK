# Onyx Panel Composition + App Chrome — Design

> Design spec for an OnyxSDK genericization pass driven by its 2nd consumer (SCUMMRedux). Today the engine **force-registers a fixed set of 8 generic panels** on every consumer, hardcodes the window title `"God Of War Toolkit"`, and (separately) hardcodes the Open-dialog game list. This makes a non-GoW app (SCUMMRedux) show irrelevant panels (ISO/PAK Browser, Camera, Anim Curves, Dopesheet, WAD Stats) it can't opt out of. This spec makes the panel set **app-composable**, the title **configurable**, and documents the already-app-settable **accent** color.

## Context & goals

OnyxSDK is a game-asset-explorer SDK extracted from GoWToolkit; SCUMMRedux is its 2nd consumer. **Goal #2 of the project is to evolve Onyx into a strong generic base** — each milestone asks "what here is generic vs app-specific?". M2a (SCUMMRedux's GUI) surfaced that the engine imposes a GoW-shaped UX on all consumers. This pass fixes that without regressing GoWToolkit.

**Important correction to the initial diagnosis:** the 8 engine-registered panels are *not* GoW-type-coupled at the code level — they operate only on Onyx-generic types (`AssetEntry`/`AssetContainer`/VFS/`AnimationData`). The truly GoW-specific panels (`WadBrowser`, `Inspector`) already live in the GoWToolkit app. The real problem is **forced registration with no opt-out**: the engine decides the panel set, not the app.

## Founding decision (user-approved)

**Minimal core + public opt-in panels.** The engine registers only a minimal, universally-useful core; the rest become public and opt-in; each app composes its panel set in its registrar.

- **Core (always registered by the engine):** `StatusBar`, `SettingsWindow`. These are useful for any consumer (log/task progress; UI preferences).
- **Opt-in (public, app adds what it wants):** `IsoBrowser`, `PakBrowser`, `CameraPanel`, `AnimCurveView`, `Dopesheet`, `WadStatsView`.
- Apps may also subclass/modify the opt-in panels (hence public headers, not just on/off flags).

## Current state (audited)

- `OnyxSDK Source/App.cpp` `App::registerPanels()` instantiates all 8 panels, then calls the app's injected registrar. All 8 panel classes live in **private** `Source/Ui/**` (only `IPanel.h`/`PanelRegistry.h` are public).
- Window title hardcoded at two sites: `Source/Window/Window.cpp` (`glfwCreateWindow(..., "God Of War Toolkit", ...)`) and `Source/App.cpp` (`TitleBar::draw(m_window, "God Of War Toolkit", ...)`).
- Accent color is **already app-configurable**: `AppConfig{ float accentR,accentG,accentB,accentA }` (default ~#E02626) → `AppConfig::getAccent()` → applied at `Window` init via `Onyx::Theme::ApplyTheme(config.getAccent(), config.themeMode)`. No engine change required to set it.
- GoWToolkit's registrar (`Apps/GoWToolkit/Source/AppRegistration.cpp` `InstallGoWPanels`) currently adds only its GoW-specific `WadBrowser` + `Inspector`; it relies on the engine for the 6 generic panels.

## Design

### OnyxSDK (engine)

1. **Public opt-in panel surface.** Promote the 6 opt-in panel classes to public headers under `Include/Onyx/App/Panels/` (e.g. `IsoBrowser.h`, `PakBrowser.h`, `CameraPanel.h`, `AnimCurveView.h`, `Dopesheet.h`, `WadStatsView.h`); implementations stay private in `Source/Ui/**`. Promote any private transitive header a public panel header requires (same approach as the M3c public-surface pass). The `StatusBar`/`SettingsWindow` core panels may stay private (the engine constructs them) — but expose them publicly too for symmetry if low-cost.
2. **Minimal-core registration.** `App::registerPanels()` registers only `StatusBar` + `SettingsWindow`, then invokes the app registrar (unchanged hook). Remove the 6 opt-in panels from engine auto-registration.
3. **Configurable window title.** Add `std::string windowTitle = "Onyx Toolkit";` to `AppConfig`. Use `config.windowTitle` at both hardcoded sites (`Window.cpp` create + `App.cpp` titlebar draw). The app sets it (via `app.getConfig()->windowTitle = "..."` in its registrar, or an `App::SetWindowTitle` setter that also calls `glfwSetWindowTitle` for live update — implementer's choice; config field is the minimum).
4. **Accent:** no code change; document that apps set `AppConfig::accent*` (or call `Theme::ApplyTheme`) to theme themselves.

### GoWToolkit (must not regress)

- Its registrar `addPanel`s the 6 now-opt-in generic panels (`IsoBrowser`, `PakBrowser`, `CameraPanel`, `AnimCurveView`, `Dopesheet`, `WadStatsView`) using the new public headers, in addition to its existing `WadBrowser` + `Inspector`. Sets `config->windowTitle = "God Of War Toolkit"`. Net UX: identical to today.

### SCUMMRedux

- Gets a clean slate automatically: only its `ScummTreePanel` ("Rooms") plus the `StatusBar`/`SettingsWindow` core. In its registrar, set `config->windowTitle = "SCUMMRedux"` and the accent to `#FFA200FF` (`accentR=1.0, accentG=0.635, accentB=0.0, accentA=1.0`). Bump the OnyxSDK `GIT_TAG` to the new release.

## Cross-repo sequencing & release

1. **OnyxSDK:** make the changes; keep its headless tests + `Examples/MinimalViewer` green; verify the core now registers exactly the 2 core panels and the 6 opt-in headers are publicly includable. Cut a release **v0.3.0** (from v0.2.0).
2. **GoWToolkit:** update its registrar + title; build against the new engine (iterate with `-DFETCHCONTENT_SOURCE_DIR_ONYXSDK=C:/CodingProjects/Personal/OnyxSDK`); confirm golden GOW2/GOWR tests stay green and the panels are back (no UX regression). Then bump its OnyxSDK consumption to v0.3.0.
3. **SCUMMRedux:** bump `GIT_TAG` to v0.3.0; set title + accent; `--selftest` green; manual launch shows only Rooms + core, title "SCUMMRedux", orange accent.

## Out of scope (deferred)

- **Profile-driven Open dialog** (the hardcoded GoW game list in `App::drawOpenDialog`) — same family, separate concern; SCUMMRedux keeps opening via its launch-path argument. A follow-up can make the dialog list driven by `ProfileManager::GetProfiles()`.
- **SCUMMRedux double-click perf/freeze** — the user sequenced "Onyx first"; the in-repo perf fix (cache the `.LAx` buffer; decode off the UI thread) is the next pass after this one.
- A flags/InitParams panel-selection API and an engine-registers-nothing model were considered and rejected in favor of minimal-core + public opt-in.

## Testing

- **OnyxSDK:** existing headless tests (sanity/logger/metrics/threading/theme_contrast) green; `MinimalViewer` builds + `--selftest` green; a check that `registerPanels()` registers exactly the core set (assert the panel count/names if a registry accessor exists, else verify by inspection + MinimalViewer showing no extra panels).
- **GoWToolkit:** builds against local OnyxSDK; golden GOW2/GOWR ctest green; manual sanity that the 6 panels reappear and the title reads "God Of War Toolkit".
- **SCUMMRedux:** `scummredux-gui --selftest <COMI.LA0>` green; manual launch shows only "Rooms" + core, title "SCUMMRedux", accent `#FFA200`.

## Open questions / to verify in the plan

1. **Header promotion cleanliness:** do the 6 opt-in panel headers include only public-promotable deps? Promote transitives as needed (verify when moving each header).
2. **Panel-count assertion:** is there a public accessor on `App`/`PanelRegistry` to assert "core registers exactly N panels" headlessly, or is verification by inspection + MinimalViewer? (Determines whether the OnyxSDK change gets an automated regression test.)
3. **Title live-update:** is a `glfwSetWindowTitle`-backed setter worth it, or is the `AppConfig.windowTitle` field (read at window create + titlebar draw) sufficient? (Lean: config field is enough; titlebar reads it each frame.)
4. **GoWToolkit/SCUMMRedux OnyxSDK pinning:** confirm each consumer's current `GIT_TAG` and that bumping to v0.3.0 is the right mechanism (vs a moving branch during iteration).
