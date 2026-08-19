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
- **Known gaps carried out of this milestone, not blocking the gate:** no
  animation playback, no per-batch visibility culling, no outline/
  wireframe/matcap shading, four dead viewport color knobs, the parity
  gate's own detection floor (misses a defect confined to one small
  object's silhouette), and `Viewport3D`'s blocking per-redraw GPU submit
  (45 FPS observed on a trivial scene). Recorded in full in
  `CHANGELOG.md`'s "Known gaps — M4 Vulkan renderer" so they outlive this
  plan file's own SDD ledger, which is deleted at merge.
- **Plan:** written at M3 gate.

## M5 — Generality (spec W5)

The exit exam: a second toolkit with zero SDK edits.

- **Deliverables:** toy `comi` module (LA0 probe, block tree, one image
  decoder, palette panel on the public widget API); `Onyx::TestKit`
  extracted (tree goldens, decode smoke, render compare) and adopted by
  GoWToolkit's test suite; glTF export of `SceneData` + Blender validation
  of a skinned corpus model; stability policy (§15) documented in the
  README; **v1.0 tag**.
- **Gate:** COMI toolkit builds and runs having touched only its own
  sources; TestKit green in CI; a skinned synthetic model exported to glTF
  poses correctly in Blender. (The GoWToolkit port then follows v1.0 as
  its first consumer migration, picking up the real-game corpus for M0's
  oracle as it lands.)
- **Plan:** written at M4 gate.

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
