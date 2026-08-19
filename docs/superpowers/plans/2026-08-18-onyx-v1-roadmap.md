# Onyx v1 Roadmap — Milestones

**Spec:** `docs/design/2026-08-18-onyx-v1-architecture.md`

Each milestone maps to one wave of the spec's port plan (§12), plus one
side-track (M0) that must land before M4. Every milestone ends buildable,
tested, and gated; a milestone's detailed task plan is written **when its
predecessor's gate passes** — writing bite-sized tasks against interfaces
that do not exist yet produces stale plans, not progress. M1's plan exists
today (`2026-08-18-onyx-v1-m1-target-split.md`); M2–M5 get theirs at the
gate before them.

```
M1 Targets ──► M2 Identity ──► M3 Modules ──► M4 Vulkan ──► M5 Generality
                                          ▲
              M0 Oracle corpus ───────────┘   (side-track, GoWToolkit repo)
```

## M0 — Oracle corpus — ✅ DONE 2026-08-18 (gate passed: corpus byte-identical twice + goldens committed + obxpak mount proven end to end) — next: M4 Vulkan

The GL renderer's reference output against which the Vulkan renderer is
proven. v1 is SDK-first (decision 2026-08-18: the GoWToolkit port follows
v1 rather than gating it), so the corpus is **synthetic and SDK-side**: an
oracle tool built on the GL SceneRenderer renders programmatic scenes — a
skinned cube mid-pose, a PBR sphere grid, an alpha-blend stack, a
200-joint palette — to PNG + per-batch JSON. Four scenes render in
ShadingMode::Solid and pin geometry/skinning/blend; a fifth,
ShadingMode::Textured sphere-grid variant pins the PBR role/metallic path
Solid's shader never exercises.

- **Deliverables:** the oracle tool (Examples/ or Tools/), the synthetic
  corpus generator (deterministic, no game data), reference images
  committed (small, synthetic — no licensing concern).
- Carried from M3: mount-aware `extract` in the generic CLI, `ByteRange`
  file identity, and uint32 offset widening — land here, where the first
  mounted module and the oracle tool need them. The corpus renderer
  (`onyx-oracle render-corpus`) covers M0's needs; a per-container
  `render <path> <node>` command in the generic CLI needs a Scene decoder
  first and lands with M4's pixel-compare harness instead.
- **Gate:** corpus renders reproducibly twice with byte-identical reports
  and pixel-identical PNGs on the same machine.
- A real-game corpus via the GoWToolkit headless harness is a post-v1
  addition when the toolkit ports; it extends the oracle, it does not gate
  M4.

## M1 — Target split (spec W1)

Three real CMake targets — `Onyx::Core`, `Onyx::Render` (GL, unchanged),
`Onyx::Shell` — plus the `Onyx::Onyx` aggregate so consumers keep linking
one name. Move and cut includes only; **zero behavior change**.

- **Deliverables:** explicit source lists (GLOB retired for target
  membership), the three targets with correct dependency edges (Core: glm +
  lz4 + toml++; Render: Core + glad; Shell: everything UI), the FFmpeg edge
  isolated into an `Onyx_Media` component target, a LayerGuard ctest that
  greps illegal includes per layer manifest.
- **Gate:** SDK tests green; GoWToolkit builds and runs against the split
  SDK with no source change; LayerGuard green in CI.
- **Entry criteria:** the concurrent logger/SessionLog branch is committed
  (the split touches `Tests/CMakeLists.txt` and `Source/Services/` lists).
- **Plan:** `2026-08-18-onyx-v1-m1-target-split.md` (written).

## M2 — Identity & state (spec W2) — ✅ DONE 2026-08-18 (branch feat/onyx-v1-m2)

The persisted-state break: string type keys, TOML settings, diagnostics and
jobs plumbing in Core.

- **Deliverables:** `TypeKey`/`TypeSpec`/`TypeRegistrar` with namespaced
  minting; persistence writes string keys everywhere (visibility, layouts);
  TOML settings store with the three scopes (app / module / workspace) and
  ~~a **one-way GTKC→TOML importer**~~ (obsolete — found already-TOML on 2026-08-18: AppConfig shipped the clean break in v0.6.0); `Diag`/`DiagSink` with salvage policy;
  `Progress` + cooperative cancel; `EventBus` (Workspace-owned, RAII
  subscriptions, id-only payloads).
- **Gate:** real GTKC configs from both machines import losslessly; SDK +
  GoWToolkit goldens unchanged; new unit tests for registrar namespacing,
  TOML round-trip, diag salvage, bus subscription lifetime.
- **Plan:** written at M1 gate.

## M3 — Modules (spec W3) — ✅ DONE 2026-08-18 (gate passed: contract tests green, example module end-to-end, Jean's GUI eyeball approved) — next: M0 oracle, then M4 Vulkan

The contract swap: `IGameModule`, evidence-based probe, `DecoderRegistry`,
`Workspace` as composition root; both GoW modules ported; the generic CLI.

- **Deliverables:** `IGameModule`/`ProbeInput`/`ProbeResult`/`MountSpec`/
  `ContainerContext`/`ModuleState`; `Workspace`+`Document` replacing
  AssetDatabase/registry singletons; TypeSpec data replacing ITypeHandler
  presentation virtuals; decoders keyed by output; the generic CLI
  (`probe/list/extract/decode`; render moved to M0 with the oracle
  tool); `MaterialDesc` with explicit `TextureRole`s replacing
  positional layers + `pbrLayers`; a complete example module (synthetic
  container format) exercising every contract.
- **Gate:** the example module drives browse/decode/CLI end-to-end;
  contract unit tests green; `IAssetProfile` and `ProfileManager` deleted
  from the SDK tree. (The gow2/gowr modules and the GoWToolkit port are
  post-v1 consumer work; `ITypeHandler` survives only as long as that
  consumer needs it.)
- **Plan:** written at M2 gate.

## M4 — Vulkan (spec W4) — ✅ DONE 2026-08-19 (gate passed on hardware; lavapipe leg is CI-authored and locally lint-checked but has never actually run — see caveat below) — next: M5 Generality

`Onyx::Render` rewritten on Vulkan 1.3 (dynamic rendering, VMA),
offscreen-first, two floors (`SceneRenderer` ready path; `RenderContext`
raw handles + `AddPass`). GL deleted after the gate.

- **Deliverables:** device/queue/swapchain bootstrap with headless target
  as the primary path; PBR pipeline on role materials; skinning; grid,
  skeleton overlay; pixel-compare harness running the M0 corpus; lavapipe
  job in CI; GL sources removed.
- Carried from M3, all landed this milestone:
  - **(DocumentId, tab) association + close viewer tabs on
    `DocumentClosed`** — **DONE at T13.** Every tab `OpenSelection` opens
    now records the owning `DocumentId`; closing that document closes
    every tab it owns.
  - **Async decode via JobQueue for heavy assets** — **DONE at T13.**
    `OpenSelection`'s decode runs on the Workspace's `JobQueue`
    (`kDecodeLane`) with a placeholder tab until `Done` fires, and
    cooperative cancellation if the document closes mid-decode.
  - **Scene branch in CLI `decode`** (spec §11, so CLI and GUI routing
    cannot diverge) — **DONE at T14.** `CmdDecode` gains a Scene branch
    ahead of Image/Text, matching `RouteForType`'s own priority; OnyxBox
    gained a mesh entry kind to exercise it.
  - **Per-container CLI `render <path> <node>` command** — **DONE at
    T14.** `onyxbox-cli render <container> <entry> --out out.png`
    (`Examples/OnyxCli/Render.cpp`) decodes a Scene and renders it
    headlessly through `VkContext`/`SceneRendererVk`/`OffscreenTarget`,
    gated `OnyxCliRender` (`SKIP_RETURN_CODE 77`).
  - **Making `Onyx_Render` link-complete standalone** (no more
    `AppConfigStub.cpp`) — **CLOSED at T11.** `ResolveRoleIndices` was
    extracted to neutral `Rendering/RenderBatch.{h,cpp}` and `Camera`'s
    `AppConfig::Get()` call moved to `Viewport3D`'s constructor (the one
    Shell class that owns both); both `onyx-oracle` and `onyxbox-cli` now
    link `Onyx::Render` standalone with zero stubs.
- **Gate:** corpus matches GL oracle within tolerance on hardware — passed
  (AMD RX 6750 XT; the four-tier tolerance — hard-cap delta, %differing,
  %high-delta, whole-image MAE — is adjudicated and stable across 3
  consecutive runs, folded into the single `ONYX_PARITY_ARGS` cache
  variable at T12) — **and** GL is gone from the tree (T11) **and** CI
  runs the render compare on every PR (`.github/workflows/ci.yml`, T12:
  `windows-msvc` + `linux-lavapipe`).
  **Honest caveat on "and on lavapipe":** this repo's remote has never
  received the branches this milestone was built on — pushing them is a
  pending human action — so the `linux-lavapipe` job has **never actually
  executed**. Its `VkOracleParity` gate is real (render tests run for real
  on lavapipe, not SKIP), but the tolerance values it starts from are
  single-GPU point estimates measured on real AMD hardware; lavapipe's
  software rasterizer has its own MSAA sample-position behavior, and the
  first push's CI run is expected to be a tuning event for
  `ONYX_PARITY_ARGS` (a one-line change), not a verdict on the renderer.
  The "on hardware" half of this gate is proven; the "and on lavapipe"
  half is authored and ready to prove itself the moment a human pushes.
- **macOS is unsupported for M4, not a formality gap.** Two concrete,
  unstarted blockers: `Source/App/Platform/Window_macos.mm` and
  `NativeWindow_macos.mm` still call `glfwMakeContextCurrent`/
  `NSOpenGLContext` on what is now a `GLFW_NO_API` window (leftover
  OpenGL-context calls from before the GL renderer was deleted, with
  nothing left to attach to), and `VkContext` has no
  `VK_KHR_portability_enumeration`/`VK_EXT_metal_surface` enablement
  anywhere. Nobody on this milestone can build or test macOS, so the
  `.mm` platform layer was left untouched rather than half-fixed; both
  blockers are real engineering work for whenever macOS is prioritized,
  not a checklist item.
- **Known gaps carried out of this milestone, not blocking the gate:** no
  animation playback, no per-batch visibility culling, no outline/
  wireframe/matcap shading, four dead viewport color knobs, the parity
  gate's own detection floor (misses a defect confined to one small
  object's silhouette), `Viewport3D`'s blocking per-redraw GPU submit
  (45 FPS observed on a trivial scene), a "Decoding…" placeholder tab's
  close not cancelling the decode, all decoding serialized on one lane,
  the CLI `render` command's failure modes collapsing onto one exit code,
  and zero automated coverage of the swapchain frame path. Recorded in
  full in `CHANGELOG.md`'s "Known gaps — M4 Vulkan renderer" so they
  outlive this plan file's own SDD ledger, which is deleted at merge.
- **Plan:** written at M3 gate.

## M5 — Generality (spec W5) — ✅ DONE 2026-08-19 (gate passed: audit's one blocking gap fixed and independently re-verified, TestKit green in both CI job definitions, glTF round-trip machine-verified, goldens byte-identical) — v1.0.0 tagged

The exit exam: prove a second toolkit can consume the SDK by naming public
headers alone, with zero SDK edits.

**Scope change from the original plan, made mid-milestone (Jean,
2026-08-19): the toy `comi` module, its toolkit and its palette panel were
cut** — "no second game format enters this repo." In its place, a single
task ran the same exam a built module would have: four audits (include,
link, capability, cold-start-TU) over the toolkits the SDK already ships
(`MinimalViewer`, `OnyxBox`, `OnyxCli`), producing the identical ranked
gaps list. This is **weaker in one specific, named way**: an audit proves
the surface is *reachable*; a second real toolkit would have proven it
*sufficient*. Recorded here rather than quietly absorbed into "passed as
planned," per this repo's own standing rule about not letting a scope cut
launder itself into a clean gate.

- **Deliverables (as actually shipped):** the public-surface audit
  (`docs/design/2026-08-19-public-surface-audit.md`, 6 gaps found, 1
  blocking); the blocking gap closed (`Onyx::Cli::CmdRender` now ships in
  a real library, `Onyx::CliRender`); the umbrella header broadened to a
  stated, testable inclusion rule plus `<Onyx/Render.h>`/`<Onyx/Media.h>`
  siblings; `Include/Onyx/Version.h` checked into the tree;
  `Onyx::TestKit` extracted (tree goldens, decode smoke, render compare)
  and consumed by the SDK's own suite (`OnyxTestKit` ctest entry, green in
  both CI job definitions — **not** yet adopted by GoWToolkit's test
  suite, since the GoWToolkit port itself is post-v1, see below); glTF
  export of `SceneData` via `Onyx::Exchange`, machine-verified by
  `cgltf_validate` plus a read-back round-trip test; `Onyx::Rendering::
  RenderToImage`, the ready floor's one-call render entry point;
  stability policy (§15) documented in the README, now in its post-1.0
  form; the version bump to `1.0.0` and the **v1.0.0 tag** (annotated,
  local only — not pushed, per standing instruction).
- **Gate:** the audit's one blocking gap (G1) fixed and independently
  reproduced (cold-start compile-AND-link of a throwaway consumer TU);
  `OnyxTestKit` registered and green in both CI job definitions (not yet
  run on a real GitHub runner — see the lavapipe caveat below); glTF
  export round-trip machine-verified (`OnyxGltf` ctest); full serial suite
  green (52/52) with goldens byte-identical
  (`git diff --stat -- Tests/Golden` empty) and parity/reproducibility
  stable across repeated runs throughout the milestone.
  **Two human gates are PENDING, not satisfied by this gate passing:**
  Blender validation of the exported glTF skinned corpus model against a
  real DCC tool (`docs/gltf-validation.md` states this explicitly), and
  the first push of this repo's history to its remote, which is what
  would finally exercise the `linux-lavapipe` CI leg end to end — it has
  authored, lint-checked YAML but has never executed on a real runner.
- **Plan:** written at M4 gate; revised mid-milestone for the `comi` cut
  (see scope-change note above).

## v1.0 — shipped

All five milestones (M0 Oracle corpus, M1 Target split, M2 Identity &
state, M3 Modules, M4 Vulkan, M5 Generality) are DONE and gated as
recorded above. `v1.0.0` is tagged locally (annotated, not pushed —
pushing is a pending human action). Full milestone-by-milestone detail and
every carried-forward known gap: `CHANGELOG.md`'s `## v1.0.0` section.

## Post-v1 backlog

Not blocking the v1.0.0 tag; recorded here so it is discoverable once the
per-milestone SDD ledgers (`.superpowers/sdd/2026-08-19-onyx-v1-m*/`) are
gone.

- **The GoWToolkit port** — the sibling repo's first migration onto the
  v1 SDK, picking up the real-game corpus for M0's synthetic oracle as it
  lands, and the consumer that would finally exercise `Onyx::TestKit`
  from outside this repo.
- **The M4 deferrals that remain** — no animation playback on the Vulkan
  renderer (below), no per-batch visibility culling, no outline/
  wireframe/matcap shading, four dead viewport color knobs, the CLI
  `render` command's failure modes collapsing onto one exit code, all
  decoding serialized on one lane, a "Decoding…" placeholder tab's close
  not cancelling its decode, and zero automated coverage of the swapchain
  frame path. Full detail: `CHANGELOG.md`'s "Known gaps" section.
- **Animation playback on the Vulkan renderer.** `SceneRendererVk` has no
  `SetAnimation`/`UpdateAnimation`/`AnimationPlayer` wiring; every skinned
  scene renders its rest pose. `Onyx::Rendering::AnimationPlayer` still
  exists and compiles, but nothing constructs one.
- **The metrics ratchet.** `onyx-oracle compare --emit-metrics` already
  writes per-scene metrics into every ctest log; wiring it into an actual
  gate is the only known path to catching a defect confined to one small
  object's silhouette, which `VkOracleParity`'s four-tier tolerance does
  not (its own documented detection floor, `CHANGELOG.md`).
- Two M5-audit cosmetic gaps not fixed this milestone:
  `Services/PathUtils.h`'s global-scope `namespace PathUtils` (the other
  half of G5; the `AssetEntry`/`AssetContainer` alias half is closed), and
  `Examples/**` still linking some raw `Onyx_*` target names instead of
  `Onyx::` aliases (G6).
- A real `install()`/`export()`/`find_package(OnyxSDK)` packaging story
  (audit G4) — documented as a deliberate non-goal for v1, not built.
- `Render.h`'s hard ImGui dependency via `AxisGizmo.h` (found at M5 T8) —
  candidate fix is moving `AxisGizmo` into `Onyx::Shell` or splitting
  `Render.h` into a headless slice plus an opt-in gizmo/debug-draw header.

## Standing rules for every milestone

- Each milestone lands via branch → spec-conformance review → merge; no
  milestone starts on top of an unmerged predecessor.
- Any Core change that needs a game name is an architecture bug (spec §13)
  — it goes back to the design, never into the tree.
- Salvage policy is watched from M2 on: CI decode-smoke runs `--strict`;
  new Error diags fail the build unless allowlisted.
- No behavior change may hide inside a refactor milestone (M1): if a fix is
  discovered mid-wave, it lands as its own commit on main first, or is
  logged and deferred.
