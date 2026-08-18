# Changelog

## Unreleased

### Added
- **`Onyx::Services::SessionLog`** (`Onyx/Services/SessionLog.h`) -- every run
  writes its own log file. `Install()` creates `logs/` next to the executable,
  prunes older sessions down to `kDefaultKeep` (10), and opens
  `onyx-YYYY-MM-DD_HH-MM-SS.log`. The names are zero padded, so sorting them
  sorts the sessions chronologically, and carry no `:`, so they are valid on
  Windows. `Window` installs it as the first thing in the process and detaches
  it as the last, which puts the whole boot and shutdown on record.
  The sink holds its handle open and flushes every line: a hard-killed process
  keeps everything logged up to the kill.
  Lines are the canonical `[LEVEL][category] message` prefixed with
  `HH:MM:SS.mmm`.
- `SessionLog::InstallAt(path)` -- the same sink at a caller-chosen path, for
  tools told where to write (a CLI's `--log run.log`) instead of wanting a
  dated name.
- `Log::AddSink(fn, minLevel)` -- sinks carry their own floor, and
  `Log::SetMemoryMinLevel` / `GetMemoryMinLevel` give the in-memory ring behind
  `Logger::GetEntries()` (what the UI shows) a floor of its own, default `Info`.
  Together they let the session file capture `Debug` while the on-screen panel
  stays quiet; the global `SetMinLevel` is now the capture floor beneath both.
  The one-argument `AddSink` is unchanged for existing callers.

## v0.6.0 - 2026-08-18

### Added
- **`Onyx::Frame`** (`Onyx/Services/FrameScheduler.h`) -- explicit frame
  scheduling. `RequestAnimation(seconds)` / `RequestRedraw()` let anything
  driven by time keep the window loop awake; the loop used to infer activity
  from input state alone, which no animation can satisfy. Requests are
  self-expiring deadlines.
- **`Onyx::Appearance`** (`Onyx/Services/Appearance.h`) -- one owner for how the
  UI looks, replacing scaling/font/theme state spread across four modules.
  Inputs (`State`), measurements (`Environment`) and derived values (`Resolved`)
  are separate types; `Resolve()` and `HouseStyle()` are pure, so the whole
  derivation is unit-tested with no window or GL context. Panels call
  `Mutate()`; `Commit()` applies everything once per frame, outside the ImGui
  frame. Design and rationale: `docs/design/appearance-system.md`.
- **UI Gallery panel** (`Onyx::App::UiGallery`, `Onyx/App/Panels/UiGallery.h`) -- a
  UI test mode registered by `App` and hidden by default. Six pages: widget
  wrappers beside their plain ImGui counterparts in each state, a live theme
  editor with a per-colour WCAG contrast audit, typography/scale controls, a
  searchable grid over all SF Symbols, live `ImGuiStyle` editors with
  "Copy as C++", and frame diagnostics plus the Dear ImGui tool windows. Open it
  from View -> UI Gallery, with F1, or `MinimalViewer --ui-test`. Nothing it
  changes is persisted -- Settings still owns `onyx.toml`.
- `App::setPanelVisible(name, visible)` so an app can open a registered panel
  from a command-line flag or its own menu.
- `Onyx::Fonts::IconTable()` / `IconCount()` (`Onyx/Fonts/IconTable.h`): the
  SF Symbols name -> glyph table, for building icon pickers.
- The gallery carries a global UI-scale bar above the tab bar (slider plus
  1x/1.2x/1.5x/2x presets and the resulting text size), so the setting that
  changes how every other page looks is reachable from all of them.
- The Icons page has its own preview-size slider, independent of the UI scale.
  It drives the glyph size through `PushFont(nullptr, size)`, so ImGui 1.92
  re-rasterises each symbol at the requested size instead of magnifying the
  14px bake -- the icons stay sharp up to 8x.
- `Onyx/Version.h`, generated from the `project()` version, with
  `ONYX_VERSION_STRING` and `Onyx::Version()`.
- Unit tests for TypeCatalog/TypeRegistry (including the v0.5.2 lazy-index
  regression), RecentFiles, AssetVisibility, ProfileManager and PathUtils --
  16 ctest entries, up from 11.
- `.clang-format`, `.editorconfig` and `.gitattributes` describing the house
  style and normalising line endings.

### Fixed
- Colour and theme changes stepped visibly. The window slept at ~15 fps unless
  input said otherwise, so a 0.25s ease-out landed in about four frames; and
  "any window is a separate OS viewport" in the same test pinned the loop at
  full speed forever once a panel was undocked (~82 fps with nothing moving),
  which is why the stutter seemed to vanish with a floating window open.
  Measured after: 13.1 fps idle, 45 frames through the transition.
- Window controls (minimise/maximise/close) were drawn at the UI font size, and
  an SF Symbol fills its em box -- ~16px of ink in a ~19px title bar. They are
  now a fraction of the bar height, and track the UI scale.
- The font size crept upward on its own (14 -> 15 -> 17 over a session, each step
  x the UI scale). `SettingsWindow::Init` baked the atlas outside the owner,
  re-seeding `style.FontSizeBase` -- which ImGui keeps as a live mirror of the
  font stack, not as a setting -- so a panel's cached copy of the size could read
  the drawn value back and submit it as the base. `Appearance::Commit` is now the
  only caller of `Fonts::BuildAtlas`, and the panels render the state instead of
  caching it.
- **UI scale only scaled the widgets, not the text.** `ScaleAllSizes` moves
  padding/rounding/border metrics; since ImGui 1.92 the drawn text size is
  `FontSizeBase * FontScaleMain * FontScaleDpi` and nothing was driving those,
  so raising the scale grew every box around 14px text. `ApplyStyleScale` now
  sets both factors (user intent and monitor scale); glyphs are rasterised on
  demand at the resulting size, so text stays crisp.
- **The bundled SF Mono was never selected as the default font.**
  `FindFontIndex("")` matched the ProggyClean entry, whose path is also empty,
  so a config with no saved choice resolved to the bitmap default instead of
  falling through to `DefaultFontIndex()` -- and then persisted that back to
  `onyx.toml`. An empty query now returns -1, as "not found" always meant.
- **Icons sat right of centre inside icon buttons.** `Widgets::IconButton` now
  draws the frame itself and places the pen so the glyph's *ink* rect lands in
  the middle of the button, instead of delegating to `ImGui::Button`, which
  centres the text box -- i.e. the glyph's advance. SF Symbols carry asymmetric
  side bearings, so the two differ: measured over 43 icons, the mean absolute
  offset went from 2.67px to 0.49px (the remainder is pixel-grid rounding).
  The first attempt at this only relaxed the advance clamp below and was not
  enough -- the measurement is what caught it. The SF Symbols merge
  pinned both `GlyphMinAdvanceX` and `GlyphMaxAdvanceX` to the icon size;
  clamping the maximum shrinks a glyph's advance without moving its ink, so
  every symbol whose natural advance was wider drew past the box ImGui measures
  and centres. Only the minimum is enforced now.
- `Widgets::SmallButton` no longer inherits ImGui's zero vertical
  `FramePadding`, which left the label touching the 1px frame border.
- The UI Gallery crashed with "Must call EndChild() and not End()!" as soon as
  a section scrolled out of view: `EndSection()` sat inside the `if`, but
  `EndChild()` must run even when `BeginChild()` returns false.
- The Style page's "Copy as C++" emitted `ImVec2(10f, 10f)` -- valid C#, not
  C++ -- and truncated fractional values.
- The Windows FFmpeg download 404'd on every fresh configure: the pinned URL
  pointed at an asset under the rolling `latest` tag, which upstream deletes as
  it moves on. Now pinned to a dated (immutable) BtbN autobuild of the same
  FFmpeg 7.1, overridable with `-DONYX_FFMPEG_WIN_URL=<zip>`.
- Repaired mojibake across 62 source files -- comments and UI strings that had
  been round-tripped through cp1252 (some two and three layers deep) showed as
  `â"€â"€` / `Ã¢â‚¬` instead of box-drawing rules, em dashes and arrows. All 116
  BOMs stripped and line endings normalised to LF.

### Changed
- Colour joined the appearance owner: the palette is a value type, the ease-out
  is a pure `Lerp(from, to, EaseOut(t))`, `Commit` is the only writer of
  `style.Colors`, and per-slot overrides are inputs on `State` that persist to
  `onyx.toml`. `ThemeManager` keeps its colour maths and becomes a facade, so
  consumers keep compiling. One semantic change: `ApplyTheme` records intent and
  the palette lands on the next `Commit` rather than inside the call.
- Factory defaults are the values settled on in the UI Gallery: 1.0 scale, 15px
  text, bundled SF Mono, and the project accent.
- Onyx's house style sizes (window padding 10, frame/tab rounding 5, grab
  rounding 4, 1px frame border) now live in `Theme::ApplyStyleDefaults()` and
  are applied before `Scale::Init()` snapshots the base style. Previously the
  app ran on stock Dear ImGui proportions, and the first UI-scale change reset
  the look to them.
- **`Source/` now mirrors `Include/Onyx/` folder for folder.** `Source/Core/`
  and `Source/Ui/` are gone: services moved to `Source/Services/`, the app shell
  and its panels to `Source/App/` (+ `App/Panels/`, `App/Platform/`), public
  viewers to `Source/Viewers/`, and Vfs/Types/Schema/Audio/Domain/Fonts/Platform
  to their matching folders. Only quoted internal includes changed; no public
  header moved.
- The SF Symbols icon table is generated from `SFSymbols.h` at build time
  instead of being a hand-maintained copy -- `FontDebuggerWindow.cpp` drops from
  6794 to 118 lines, and the table can no longer drift from the font.
- Renamed the last GoW-era identifiers: `GOWTOOL_OS_*` -> `ONYX_OS_*`,
  `gowtool_isatty/fileno` -> `onyx_*`.

### Breaking
- The dockspace id changed (`GoWToolDockSpace` -> `OnyxDockSpace`), so a saved
  `imgui.ini` starts from the app's default layout once.
- The recents file is now `onyx_recents.txt` (was `gowtool_recents.txt`); the
  old list is not migrated, matching the `gowtool.gtkc` -> `onyx.toml` break.

## v0.5.3 - 2026-08-17

### Fixed
- The global selection (`Onyx::Api::GetSelected` / `GetSelectedWad`) is now
  cleared when a container closes. It holds raw pointers into
  `AssetDatabase::wads` / `::paks`; `CloseWad` erases the vector element and
  frees the `AssetEntry` storage, so the Inspector's per-frame draw, App's
  copy-hash action and the browsers' highlight test all dereferenced freed
  memory on the next frame -- closing a WAD with an entry selected crashed the
  app. `Init()` now subscribes to `EventWadClosed` / `EventAllClosed`, both of
  which are posted before the vectors are mutated.

## v0.5.2 - 2026-08-17

### Fixed
- `TypeRegistry` now builds its `TypeId` -> handler index lazily on the first
  `Resolve()` instead of eagerly at registration. Handlers self-register during
  static init, but `GetId()` reads app-owned `TypeId` handles that stay 0 until
  the app seeds the `TypeCatalog` inside `main()` -- so every handler was filed
  under id 0, each overwriting the last, and `Resolve(realId)` always returned
  `nullptr`. Consumers saw this as "No viewer found for TypeId=N" on every asset
  (`ViewerRegistry`'s kind-based fallback calls the same `Resolve`, so it failed
  identically). Registration now only marks the index dirty; a mutex guards the
  map since `Resolve` runs from worker threads as well as the UI. Handlers still
  reporting id 0 at index time are skipped with a warning instead of shadowing
  the Unknown sentinel, and duplicate `TypeId` claims are reported rather than
  silently overwriting.

### Added
- `TypeRegistry::InvalidateIndex()` for the rare case where a handler's id
  changes after the fact (e.g. the catalog is re-seeded between tests).

## v0.5.1 — 2026-06-20

### Changed
- Third-party libs (Dear ImGui, ImPlot, ImGuiColorTextEdit) are now consumed via
  CMake FetchContent — pinned to the exact previously-vendored/submoduled commits —
  instead of a vendored copy + git submodules. No third-party source lives in the
  repo anymore; updates are a one-line `GIT_TAG` bump.
- App config migrated from the binary `GTKC` blob to human-readable **TOML**
  (`onyx.toml`) via toml++. Old `gowtool.gtkc` is not migrated (clean break: first
  run starts from defaults). Window/dock layout stays in ImGui's native `imgui.ini`.
  Removes the vestigial `AppConfig::imguiIniState` field and two GoW-named remnants
  (`gowtool.gtkc` + the `GTKC` magic).

### Added
- The default UI font is now the bundled SF Mono when the user has no saved choice.

## v0.5.0 — 2026-06-20

Decoupled God of War from the SDK — the public surface is now game-agnostic.

### Removed (breaking)
- `Onyx::Types::GameVersion` enum.
- `TypeRegistry::RegisterByMagic` / `RegisterByTag` / `ResolveByTag` and the
  `ITypeHandler::GetMagic()` method; the `REGISTER_TYPE` / `REGISTER_TAG` macros.
  The WAD magic/tag dispatch now lives in consumers (GoWToolkit's `Onyx::Gow`).
- `Onyx::Parsers::ScriptTargetParser` (GoW-specific) and the deprecated
  `Onyx::Domain::WadAssetName`.

### Changed
- `AssetVisibility` is keyed by `TypeId` alone (no game-version dimension);
  serialized filter overrides reset once on upgrade.
- `IAssetProfile::GetHints()` added — profiles declare their own CLI aliases;
  the SDK no longer hardcodes any game hints.

### Kept (the generic path)
- `TypeCatalog::Register` + `TypeRegistry::RegisterByTypeId` + `Resolve(TypeId)`
  + `REGISTER_FILE_TYPE` — unchanged.

## v0.2.0 — 2026-06-15

Generic container primitives — the reusable half of the SCUMMRedux effort's
"what's generic → Onyx" boundary (the SDK's second consumer).

### Added
- `Onyx::Container` module:
  - `ChunkReader` — config-driven IFF/RIFF 4CC chunk reader (tag size, endianness,
    alignment, header-inclusive size, size-before-tag) over a zero-copy `std::span`.
  - `ChunkSchema` — `tag → {allowed children}` registry.
  - `ChunkNode` + `BuildChunkTree` — schema-driven, tolerant chunk-tree builder
    (unknown/oversized chunks degrade to opaque leaves; never throws).
- `Onyx::Vfs::TransformFile` + `MakeXorFile` — per-byte cipher decorator on `IFile`
  (XOR; identity when key is 0).

## v0.1.0 — 2026-06-14

Initial standalone release of OnyxSDK, extracted from GoWToolkit via `git filter-repo --subdirectory-filter Engine`. Preserves commit history from the M2 engine/app split onward.

### What's included
- `Onyx` static library (`Onyx::Onyx`) with public `Include/Onyx/` headers
- All third-party dependencies wired in: Dear ImGui (docking), GLFW, GLM, lz4, glad, miniaudio, ImPlot, ImGuiColorTextEdit, FFmpeg
- `Examples/MinimalViewer` — minimal Onyx consumer app
- `Tests/` — engine-only unit tests (sanity, logger, metrics, threading, theme contrast)
- CMakePresets for debug/release Ninja builds
