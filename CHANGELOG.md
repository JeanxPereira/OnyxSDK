# Changelog

## Unreleased

### Added
- **M4 Vulkan renderer** (v1 spec §W4) — `Onyx::Render` rewritten on
  Vulkan 1.3 (dynamic rendering, VMA), offscreen-first, on two floors:
  `SceneRendererVk` (the ready-made PBR/skinning/grid/skeleton path
  consumers use by default) and `Onyx::Rendering::RenderContext` (raw
  `VkDevice`/queue/command-buffer handles plus `AddPass`/`RemovePass`, for
  callers that need to record their own Vulkan work — a registered pass
  that throws is caught, logged and skipped without corrupting the frame
  or unregistering the pass). Device/instance/swapchain bootstrap
  (`VkContext`) supports headless (no surface) as the primary path, with
  presentation as the GUI's addition on top — platform status, honestly:
  **Windows** is verified end to end (this is where the GUI has actually
  been run). **Linux** is implemented (`VkContext::Init` takes the
  caller's `glfwGetRequiredInstanceExtensions()` result and requests
  those instance extensions) and builds green in CI (`linux-lavapipe`),
  but nobody has run the GUI against a real Linux window in this
  milestone — "implemented and builds" is not the same claim as
  "verified," and earlier drafts of this entry conflated the two.
  **macOS is unsupported** for M4: `Source/App/Platform/Window_macos.mm`
  and `NativeWindow_macos.mm` still drive a `GLFW_NO_API` window through
  `glfwMakeContextCurrent`/`NSOpenGLContext` (leftover OpenGL-context
  calls with nothing to attach to now that the GL renderer is deleted),
  and there is no `VK_KHR_portability_enumeration`/`VK_EXT_metal_surface`
  enablement anywhere in `VkContext` -- both are real, unstarted work, not
  a formality. PBR pipeline on role-indexed materials, skinning via a
  joint-palette SSBO (shared with the deleted GL renderer's own
  extraction, `Rendering::JointPalette`), procedural grid + skeleton
  overlay.
- **The parity gate ("the milestone's teeth")** — `onyx-oracle
  render-corpus --renderer vk` renders the M0 corpus through the Vulkan
  path; `onyx-oracle compare` checks it against the frozen GL goldens
  (`Tests/Golden/corpus`, produced before GL was deleted and never
  regenerated) on a four-tier tolerance tuned to separate expected 4x-MSAA
  coverage-quantization noise from a real regression: a hard per-channel
  delta cap (tripwire only), the percentage of pixels with any nonzero
  delta, the percentage with a channel delta > 8 (isolates "a few pixels
  differ a lot" edge noise), and whole-image mean absolute error (catches
  broad, low-amplitude drift the two percentage tiers structurally
  cannot). Wired as the `VkOracleParity` ctest, `SKIP_RETURN_CODE 77` when
  no Vulkan-capable device is present. See "Known gaps" below for what
  this gate does and does not catch.
- **Headless per-container render** — `onyx-oracle`'s corpus renderer is
  joined by a per-container path: the generic CLI (`onyxbox-cli render
  <container> <entry> --out out.png`) resolves an entry, decodes it as a
  Scene, and renders it headlessly through the same `VkContext` +
  `SceneRendererVk` + `OffscreenTarget` stack, gated `OnyxCliRender`
  (`SKIP_RETURN_CODE 77`, same convention). `CmdDecode` also gains a Scene
  branch ahead of Image/Text, matching the GUI's own `RouteForType`
  priority so CLI and GUI routing cannot diverge (v1 spec §11).
- Async decode: `OpenSelection`'s decode now runs on the Workspace's own
  `JobQueue` instead of the UI thread, with a placeholder tab standing in
  until the job's `Done` callback lands on a later `Pump()` and
  cooperative cancellation if the document closes mid-decode. Every tab
  `OpenSelection` opens now records the `DocumentId` it belongs to, and
  closing that document closes every tab it owns (`DocumentClosed` on the
  Workspace's `EventBus`).
- **First CI** (`.github/workflows/ci.yml`) — `windows-msvc` (Ninja +
  MSVC, GPU-less hosted runner: every render-labeled ctest entry SKIPs via
  its `SKIP_RETURN_CODE 77`, the suite still ends green) and
  `linux-lavapipe` (Mesa's software Vulkan implementation: render tests
  including `VkOracleParity` actually run and gate every PR — see "Known
  gaps" below for why this leg's first real run is a tuning event, not a
  pass/fail verdict). The four parity tolerance knobs are one CMake cache
  variable now (`ONYX_PARITY_ARGS`, `Tools/OnyxOracle/CMakeLists.txt`), so
  a lavapipe re-tune is a one-line change.
- **M0 GL oracle corpus** -- the reference the Vulkan renderer must match:
  `onyx-oracle` (Tools/OnyxOracle) renders five deterministic synthetic
  scenes offscreen to PNG plus a canonical byte-stable JSON report
  (`std::to_chars` formatting, locale-proof). Four render in
  ShadingMode::Solid and pin geometry/skinning/blend (PBR sphere grid,
  skinned cube posed rest-vs-bind, alpha-blend stack, 200-joint spiral);
  a fifth, ShadingMode::Textured sphere-grid variant pins the PBR
  role/metallic path Solid's shader never exercises. Golden references
  live in `Tests/Golden/corpus`; `OracleReproducible` proves two
  independent Vulkan render-corpus runs are byte-identical
  (`SKIP_RETURN_CODE 77` when no Vulkan-capable device is present) —
  matching those goldens is `VkOracleParity`'s job (see "the milestone's
  teeth" above), not this test's. `OracleMatchesGolden` (a GL-render
  ctest) died at Task 11 along with the GL renderer it existed to gate;
  it is not part of this suite anymore.
- **Mounts at open** (v1 spec §5.2) -- a module's `MountSpec` now runs:
  the Workspace mounts matching archives at open, documents own a file
  table (slot 0 = the container) plus the mounted VFS, and module-thrown
  mount exceptions are contained as diags with a flat-file fallback.
  OnyxBox gains a mounted `.obxpak` archive proving the chain end to end
  through the CLI, including decode disambiguation across same-named
  inner entries.
- `Parsers::MaterialDesc` carries a PBR `metallic` factor and the
  renderer flows it into batches.
- **M3b Shell on the Workspace** — documents open through GameModules
  end to end in the GUI: generic Documents browser (TypeSpec-driven tree,
  Failed tint, positional selection paths), viewers routed by decoder
  capability (Scene > Image > Text), Inspector panel on SelectionChanged,
  status bar with live open progress; Workspace gains CancelOpen and a
  settings path; the CLI gains `--game` and consistent exit codes.
- **M3a module contracts** (v1 spec §5, §6, §11) — the SDK's game-facing
  surface:
  - `Modules::IGameModule` — probe/types/decoders/parse in one contract;
    detection is evidence-ranked (`RankProbes`: confidence + reason per
    module, floor 40, ties mean nobody wins).
  - `Modules::Workspace`/`Document` — the composition root: owns the
    EventBus, JobQueue, settings and decoder registry; open is
    salvage-first (a partial tree with diags is a result, not a failure),
    async parses cannot be freed under a Close, and module exceptions
    never cross the boundary.
  - `Modules::DecoderRegistry` — decode capabilities keyed by output
    (Scene/Image/Text); a throwing decoder yields diags, never a crash.
  - **OnyxBox** (`Examples/OnyxBox`) — a synthetic module proving the
    contracts end to end; hardened against hostile containers (TOC count
    clamp, lying image headers).
  - `Cli::Commands` — generic `probe`/`list --json`/`extract`/`decode
    --strict` for any Workspace; extract refuses unsafe entry names.
    Example binary `onyxbox-cli`.

### Changed
- **BREAKING: the OpenGL renderer is deleted.** `Onyx::Render` is Vulkan
  1.3 only now — glad, the GLSL 330 shaders and the GL `SceneRenderer` are
  gone from the tree entirely; there is no compatibility shim. Consumers
  present through the Vulkan surface (`Include/Onyx/RenderVk/`) described
  above. The GL oracle corpus's goldens (`Tests/Golden/corpus`) are kept,
  frozen, as the permanent parity anchor the Vulkan renderer is measured
  against — they are not regenerated by a GL build that no longer exists.
- **BREAKING:** entries address their payload through
  `Domain::ByteRange` (`source.fileIndex/offset/size`, 64-bit) instead
  of raw `uint32_t offset/size` fields (v1 spec §5.4); mounted entries
  extract into per-file-index subdirectories.
- **BREAKING:** materials carry explicit texture roles (`TextureRole`,
  `MaterialDesc`); `MaterialInfo`, `pbrLayers` and the nested per-layer
  texture vectors are gone — `SceneData::textures` is a flat pool indexed
  by role. Consumers pinned to v0.6.x are unaffected until they port.
- **M2 identity & state services** (v1 spec §4, §7) — five new Core services,
  all UI-free and unit-tested:
  - `Types::TypeRegistrar` — module-namespaced type minting ("gowr.mesh");
    cross-module key collisions are impossible by construction.
    `TypeCatalog::KeyOf(TypeId)` provides the reverse lookup.
  - `Services::DiagSink` — diagnostics as data (`Diag{severity, code,
    message, byte ref}`), thread-safe collect/drain; the salvage policy's
    backbone.
  - `Services::JobQueue` — lane-serialized worker pool with cooperative
    cancel; Done callbacks fire only in `Pump()`; a throwing job is contained
    and cannot wedge its lane.
  - `Services::EventBus` — workspace-ownable typed pub/sub; RAII
    `Subscription`; FIFO dispatch on `Pump()`; documented lifetime contract.
  - `Services::Settings` — scoped TOML settings with exact-typed reads
    (a mismatched key reads as absent, never coerces).
- Visibility overrides persist by **type key string**, not numeric id
  (`AssetVisibility::SaveOverrides`/`LoadOverrides`; `AppConfig`'s
  `[visibility]` block migrated — old numeric entries are dropped silently,
  the accepted one-time reset).

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

### Removed
- **BREAKING:** the profile-era loading layer is retired — `IAssetProfile`,
  `ProfileManager`, `AssetDatabase`, the Iso/Pak browsers and the
  raw-pointer asset events are gone. Consumers pinned to v0.6.x are
  unaffected until they port.
- **BREAKING (M5 T8 fix round, spec §15/G5):** the global-scope
  `::AssetEntry` backwards-compat alias is gone from both
  `Include/Onyx/Domain/Entry.h` and `Include/Onyx/Services/
  AssetVisibility.h` — use `Onyx::Domain::AssetEntry` (or
  `Domain::AssetEntry` from inside any `Onyx::*` namespace) instead. Two
  global-scope declarations of the same alias were a pre-1.0 hygiene leak
  (audit gap G5); removing it now is a MINOR bump, the same fix after
  v1.0.0 would need a MAJOR one for a pure namespace-hygiene change.
  `Include/Onyx/Types/ITypeHandler.h` keeps its own, independent
  global-scope `AssetEntry`/`AssetContainer` aliases — a third, unrelated
  copy of the same pattern this fix did not touch; a follow-up should
  fold it in too.

### Known gaps — M4 Vulkan renderer
Recorded here on purpose so nobody rediscovers these by surprise once the
milestone's working ledger (`.superpowers/sdd/2026-08-19-onyx-v1-m4-vulkan/`)
is gone at merge. None of these are silent: every one is either disabled
with an in-UI tooltip, documented in the source at the exact spot a reader
would look, or both.
- **No animation playback.** `SceneRendererVk` has no
  `SetAnimation`/`UpdateAnimation`/`AnimationPlayer` wiring this milestone
  — every skinned scene renders its rest pose. The GL path's transport
  bar/clip browser/play-pause UI was removed with it rather than left
  wired against nothing. `Onyx::Rendering::AnimationPlayer` still exists
  and compiles, but nothing constructs one:
  `Onyx::App::GetActiveAnimationPlayer()` is permanently null, so
  `Dopesheet` and `AnimCurveView` degrade gracefully (no crash, just
  nothing to show) rather than being deleted outright.
- **No per-batch visibility culling.** `Render()` does not read
  `RenderBatch::isVisible`/`isHighlighted` — the Inspector's visibility
  checkboxes and hover highlight still exist and still mutate those
  fields (the same `Rendering::RenderBatch` struct the deleted GL renderer
  also filled), they simply have no effect on what gets drawn yet.
- **No outline/hover-highlight pass, no wireframe or matcap shading.**
  `ShadingMode::Matcap`/`Wireframe`/`TexturedWire` were alias-only on the
  GL renderer already (they silently rendered as Solid/Textured); the T11
  fix round removed all three from the shading-mode combo and both
  toolbar cycle sites rather than keep offering choices with no effect.
  Only `Solid` and `Textured` remain.
- **Four viewport color pickers are dead knobs.** Bones/Wireframe/Outline
  colors (`AppConfig::boneR`/`wireR`/`hlR`) have zero readers at HEAD —
  the skeleton overlay draws every frame with its own hardcoded color, so
  a picker that looks like it controls it but doesn't would be actively
  misleading. All three are disabled in Settings with a tooltip
  explaining why; the config fields keep persisting so a real reader can
  pick them up later with no migration. (The Matcap picker that used to
  sit beside these was removed outright at T11 — no Vulkan path reads it
  at all, disabled-with-tooltip would have been describing a feature that
  no longer exists in any form.)
- **The parity gate's honest detection floor.** `VkOracleParity`'s
  four-tier tolerance (see above) catches a defect that touches more than
  roughly 0.2-0.3% of pixels at delta>8, or one broad enough to move
  whole-image MAE past 1 LSB. It does **not** catch a defect confined to
  one small object's silhouette (~0.13% of the frame in the adjudicated
  measurements) — real teeth against that class of regression would come
  from the opt-in per-scene metrics ratchet `compare --emit-metrics`
  already emits into every ctest log, not yet wired into any gate.
- **`Viewport3D` redraws via a blocking `OneShot` submit on the UI
  thread.** Each redraw allocates its own command buffer, records the
  frame, submits, and blocks until the GPU finishes — 45 FPS observed with
  vsync on a trivial scene. It only runs when something actually changed
  (camera moved, a scene loaded, a toggle flipped), not every ImGui
  frame, mirroring the GL path's own "cache the FBO, redraw only on
  change" strategy — but the GPU round trip is synchronous where GL's
  was an implicit queue-and-continue. `RenderContext::AddPass` exists as
  the structurally right home for a frame-pipelined, non-blocking version
  of this; nothing beyond the 45 FPS single observation above has been
  measured, and no work here has started.
- **macOS is unsupported this milestone**, not a formality gap.
  `Source/App/Platform/Window_macos.mm` and `NativeWindow_macos.mm` still
  call `glfwMakeContextCurrent`/`NSOpenGLContext` against what is now a
  `GLFW_NO_API` window — leftover OpenGL-context calls with nothing left
  to attach to now that the GL renderer is deleted — and `VkContext` has
  no `VK_KHR_portability_enumeration`/`VK_EXT_metal_surface` enablement
  anywhere. Nobody on this milestone has macOS hardware to build or test
  against, so the `.mm` platform layer was left as-is rather than
  half-fixed; both blockers are real, unstarted work.
- **Closing a "Decoding…" placeholder tab does not cancel the decode.**
  `ViewerOpening.cpp`'s cancellation only fires on `DocumentClosed`
  (closing the whole document); closing just the placeholder tab the
  decode is standing in for leaves the job running on the `JobQueue` —
  the real viewer still opens seconds later even though the tab that was
  going to show it is gone.
- **All decoding is globally serialized on one lane (`kDecodeLane`).**
  There is one decode lane, not one per document or one sized to
  available cores — a second document's asset decode queues entirely
  behind whatever large decode is already running on the lane, with no
  way to jump the queue or run in parallel.
- **The CLI `render` command collapses distinct failure modes onto
  `kUsage`.** GPU-init failure, pipeline-build failure, framebuffer
  readback failure, and PNG-write failure all currently exit with the
  same usage-error code, so a calling script cannot tell "you passed a
  bad argument" apart from "the disk is full" or "there is no
  Vulkan-capable device" from the exit code alone.
- **The swapchain frame path has zero automated coverage.** Every "0
  validation messages" claim made for the Shell's actual windowed
  presentation is a manual, timeout-bounded run of the GUI, not a ctest —
  no test in this suite creates a real window and drives frames through
  the swapchain.

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
