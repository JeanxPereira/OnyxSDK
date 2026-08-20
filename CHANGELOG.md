# Changelog

## v1.1.0 — 2026-08-19

Skeletal animation returns to the Vulkan renderer. `SceneRendererVk` had no
playback path since M4 deleted the GL renderer — every skinned scene
rendered its rest pose, and the Shell's transport bar/clip browser were
removed rather than left wired against nothing (see v1.0.0's "Known gaps").
This tag closes that gap end to end: renderer API, GPU upload path, the
parity-style gate that proves it, and the Shell UI that reaches it. One
public-header signature changed along the way (`RenderSkeleton`, see
"Changed" — classified BREAKING under this project's own stability policy)
and one pre-existing bug surfaced and was fixed (`ComputeJointMatrices()`'s
dropped inverse-bind correction, see "Fixed").

### Added
- **`SceneRendererVk`'s animation API** (`Include/Onyx/Rendering/
  SceneRendererVk.h`) — `HasAnimations()`, `GetAnimationData()`,
  `GetAnimPlayer()`, `SetAnimation(int groupIdx, int actIdx)`,
  `StopAnimation()`, `UpdateAnimation(float dt)`. The names deliberately
  mirror the deleted GL `SceneRenderer`'s own method names — the header's own
  comment says so — so a consumer porting call sites off the old GL API does
  it mechanically, receiver-swapped, with no signature translation. That is
  exactly how the Shell wiring below was ported back.
- **The joint palette SSBO is host-visible and persistently mapped**,
  rewritten in place per pose change via `UploadBatchPalettes()` (a memcpy
  per batch into already-mapped memory, no allocation, no command buffer).
  This rests on a serialization PRECONDITION now documented as a named
  banner at the top of `Include/Onyx/Rendering/SceneRendererVk.h`: every
  scene submission goes through `Resources::OneShot`, which submits and then
  blocks on `vkWaitForFences` before returning, so the CPU is never writing
  a buffer the GPU is still reading — no double-buffering, no barrier,
  because there is never a second frame in flight for a scene command
  buffer. The banner is explicit that this precondition covers every other
  mapped buffer in the file (frame UBOs, overlay VBO, background/grid UBOs),
  not just the palette, and that an in-flight scene path would break all of
  them at once, not just this one.
- **Per-batch visibility culling.** `Render()`'s three passes (sky, opaque,
  additive) each skip a batch when `!m_batches[idx].isVisible`, including
  skipping pipeline binding for a culled batch in the additive pass, not
  just the draw call. The Inspector's visibility checkboxes, which already
  mutated `RenderBatch::isVisible` with no effect, now work. `isHighlighted`
  is still unread — no outline pass exists on this renderer (see "Known
  gaps").
- **The bone color picker is re-enabled.** `RenderSkeleton` now takes a
  `boneColor` parameter (see "Changed"); `Viewport3D::RenderFrame` resolves
  it from `AppConfig::boneR/G/B` the same way it already resolves
  `gridColor` for `RenderGrid`, and the picker's `BeginDisabled` in
  `SettingsWindow.cpp` was removed. `Wireframe Color` and `Outline Color`
  stay disabled — their passes genuinely don't exist on this renderer.
- **`VkAnimation`**, a new ctest (`Tools/OnyxOracle`'s `--vk-animation-smoke`,
  wired through `RunVkAnimationSmoke()`): renders one synthetic animated
  scene five ways from a single `SceneRendererVk` instance and compares them
  pairwise against render (a), plus a boolean assertion on `UpdateAnimation`'s
  return value after a stop.
  - (a)-vs-(b) *animation set, not advanced*: byte-identical, 0.0000%
    differing. This is the gate's correctness anchor, not merely a "nothing
    changed" smoke check — it means the animated pose at t=0 reproduces
    `ComputeJointPalette`'s independently-computed rest pose exactly, which
    is only possible if the inverse-bind matrix used on the animated path is
    right.
  - (a)-vs-(c) *mid-clip*: `maxChannelDelta=255 differingPct=22.1565%
    highDeltaPct=21.6438% mae=20.1479`, correctly asserted **outside**
    tolerance — proof the pose moves. This is a motion check, not a
    correctness check on its own: a renderer that moved the pose *wrong* in
    a way that still produced a large, non-uniform delta would also pass
    this comparison. The (a)-vs-(b) anchor above is what pins correctness;
    this pins that something is actually happening.
  - (a)-vs-(d) *stopped*: byte-identical, 0.0000% differing — the rest pose
    is restored exactly, not merely close.
  - (a)-vs-(e) *one batch culled*: `maxChannelDelta=229 differingPct=17.6334%
    highDeltaPct=17.6334% mae=17.2809`, correctly asserted outside tolerance
    — proves the visibility-culling addition above actually skips drawing.
  - (a)-vs-(f) *one ordinary frame tick after stop*: byte-identical
    pixel-wise, but this comparison **cannot discriminate** a correct no-op
    from a stale-sentinel bug that recomputes and reuploads the rest pose
    (see "Fixed" below) — `EnsureBaked()`'s frame-0 bake and `Stop()`'s
    restore both compute the identical bytes, so a wrong recompute writes
    the same pixels as no recompute at all. The actual regression check is
    the boolean assertion: `UpdateAnimation(0.016f)` immediately after
    `StopAnimation()` must return `false` (nothing changed); reverting the
    sentinel fix flips this to `true` and fails the gate
    (`***Failed 0.47 sec with "returned true (expected false)"`).
- **`BuildAnimatedChain()`** in `Tools/OnyxOracle/CorpusScenes.{h,cpp}` — a
  synthetic skinned-cube clone whose skeleton keeps `BuildSkinnedCube()`'s
  existing 30-degree rest bend on joints 1/2 intact, with a clip sweeping
  those same joints 60→120 degrees over a 1-second, 31-sample duration.
  Deliberately **not** called from `BuildCorpus()`, so it is invisible to
  the parity gate and the frozen goldens (`Tests/Golden/corpus`) never move
  — confirmed by `VkOracleParity`'s five pixel hashes staying byte-identical
  before and after this milestone's every commit.
- **The Shell's animation transport bar and clip browser are back** in
  `Viewport3D` (`Source/Viewers/Viewport3D.{h,cpp}`): a clip selector combo
  when the scene has animations but nothing is chosen, growing additively
  (not replacing) into a full transport (play/pause/step/stop, speed, loop
  mode, timeline) once a clip is loaded — the two stack to 40px + 86px =
  126px rather than swapping, so a second clip is reachable without
  reloading the scene. `Onyx::App::GetActiveAnimationPlayer()` is non-null
  again while a clip plays, so `Dopesheet` and `AnimCurveView` receive real
  data. See "Known gaps" for why the browser could not simply be restored
  to its historical location.

### Fixed
- **`AnimationPlayer::ComputeJointMatrices()` read a dead code path for the
  inverse bind matrix, silently dropping it from every animated pose.** It
  read `m_skeleton->matrixes3[joint.invId]` — a literal port of the JS
  reference's raw-array indexing — but `joint.invId` defaults to `-1` and
  nothing in this tree (`Tools/OnyxOracle/CorpusScenes.cpp`'s synthetic
  builders are the only `ObjectData` producer that exists) ever populates
  `matrixes3`/`invId`; that path was meant for a not-yet-built real loader.
  Every other consumer — `ComputeJointPalette` (`JointPalette.cpp`, the
  frozen-golden rest-pose path) and `GltfExport.cpp` — reads the resolved
  `joint.bindToJointMat` field instead. So the branch never fired, and the
  inverse-bind correction was silently absent from every *animated* pose
  while `ComputeJointPalette`'s *rest* pose applied it correctly the whole
  time. This was invisible until this milestone because nothing drove
  `ComputeJointMatrices()` against a real skeleton until `SetAnimation()`
  existed to call it — the `VkAnimation` gate's (a)-vs-(b) comparison caught
  it on its very first run, coming back ~22% differing instead of the
  expected 0%. Fixed by reading `joint.bindToJointMat` directly, matching
  `ComputeJointPalette`'s own unconditional `globalMats[i] * j.bindToJointMat`.
  **Consumer-visible semantic delta:** the old `matrixes3` branch gated on
  `joint.isSkinned`; the new `bindToJointMat` read does not (neither does
  `ComputeJointPalette`, which this now matches) — a joint with
  `isSkinned == false` and a non-identity `bindToJointMat` now produces
  different animated output than before this fix. No loader in this tree
  can currently construct such a joint, so nothing observed the change, but
  a future real GOW loader could.
- **`StopAnimation()`'s rest-pose restore was undone by the very next
  frame.** The restore itself was correct, but `StopAnimation()` set
  `m_lastAppliedAnimTime = -1.0f` as a sentinel; `Stop()` (called just
  before it) separately reset the player's own time to `0.0f` via `Reset()`.
  The next `UpdateAnimation()` call — an ordinary per-frame tick, not a
  scrub — then saw `GetTime() (0.0f) != m_lastAppliedAnimTime (-1.0f)`, took
  the paused/scrub branch, and recomputed and re-uploaded the pose a second
  time. The recomputed bytes happened to be identical to the already-correct
  restore (both paths ultimately call the same bake/palette code), so this
  was never visible as pose corruption in this milestone's own gate — but it
  meant the "immutable rest pose after Stop" guarantee the palette-caching
  design depends on lasted exactly one frame, not permanently, and the two
  computations only agreed *because* the `ComputeJointMatrices()` fix above
  made them agree; before that fix this same sentinel bug would have been
  real, visible corruption. Fixed by setting
  `m_lastAppliedAnimTime = m_animPlayer ? m_animPlayer->GetTime() : -1.0f`
  (i.e. `0.0f`) instead, so the very next tick's scrub-detection comparison
  is `0.0f != 0.0f` — false — and `UpdateAnimation` correctly returns
  `false` (nothing changed), verified by a dedicated boolean assertion in
  the `VkAnimation` gate (see "Added" above) since the pixel comparison
  cannot tell the two cases apart.

### Changed
- **BREAKING (MAJOR-class under this project's own stability policy):
  `SceneRendererVk::RenderSkeleton()` gained a `boneColor` parameter.**
  `Include/Onyx/Rendering/SceneRendererVk.h`'s signature changed from
  `RenderSkeleton(ctx, pipeline, cmd, view, proj, viewportW, viewportH, err)`
  to `RenderSkeleton(ctx, pipeline, cmd, view, proj, boneColor, viewportW,
  viewportH, err)` — a new required parameter inserted into an existing
  public declaration, not appended with a default. This project's README
  ("Stability policy") reserves that shape of change for MAJOR: "the only
  version class allowed to remove, rename, or change the meaning of an
  existing public declaration," and a parameter insertion changes the
  meaning of every existing call site's argument list. It is classified
  BREAKING here rather than softened, per that policy read honestly, even
  though this tag ships as 1.1.0 (a MINOR bump) rather than 2.0.0 — see the
  note at the end of this entry. The reason for the change: `Onyx::Render`
  does not, and structurally cannot, link `Onyx::Shell` (where
  `AppConfig::Get()` lives) — `RenderGrid`'s existing doc comment already
  said so, and `RenderGrid` already took its color the same way, as a
  caller-supplied parameter rather than an internal config read. Only
  caller (`Viewport3D::RenderFrame`) was updated in the same commit.
  **Note on the version number:** the stability policy's own worked example
  (the `RenderVk` → `Rendering` header fold, v1.0.0) treated an equivalent
  MAJOR-class change as requiring a MAJOR bump once the project is past
  1.0. This tag is released as 1.1.0 regardless, as a deliberate scope
  decision for this milestone rather than a policy exception silently
  taken — flagged here so the inconsistency is visible rather than buried.

### Known gaps — the v1.1 promise
Animation playback, per-batch visibility culling, and the bone color knob
are removed from this list — see "Added" above. Everything else carried
forward from v1.0.0 stays, plus one new finding from this milestone's own
Shell-wiring work.
- **No outline/hover-highlight pass, no wireframe or matcap shading.**
  `RenderBatch::isHighlighted` is still unread — no outline pass exists on
  this renderer. Wireframe and Outline colors remain dead knobs, disabled
  in Settings with a tooltip explaining why (Matcap was removed outright at
  T11, v1.0.0).
- **NEW: `IDocumentContent::DrawInspector()` has no caller anywhere in the
  Shell.** `InspectorPanel::Draw()` (`Source/App/Panels/InspectorPanel.cpp`)
  draws only its own `InfoTab` and never delegates to the active document;
  `DocumentWindow::Draw()` calls `tab->Draw()`, never `tab->DrawInspector()`.
  This is pre-existing — `git log -S` finds no commit that ever added a
  caller, at this tag or at the last commit before the GL renderer was
  deleted — and it silently disables `VideoPlayer`'s stream-metadata
  inspector too, not just anything animation-related. It is the direct
  reason this milestone's clip browser moved into `Viewport3D`'s own
  viewport strip (a new `DrawClipSelector()`) instead of being restored to
  its historical home inside `DrawInspector()`: code ported faithfully into
  that hook was confirmed, by construction, to never execute. Recorded here
  so nobody puts something important into `DrawInspector()` on the
  assumption it is reachable and loses it the same way.
- **NEW, honest caveat: the mapped joint-palette (and every other mapped
  buffer in `SceneRendererVk.cpp`) assumes `HOST_COHERENT`, which VMA does
  not guarantee for `CPU_TO_GPU`** (it requires `HOST_VISIBLE` and only
  *prefers* `DEVICE_LOCAL`), and nothing in the tree calls
  `vmaFlushAllocation`. This is **not a regression introduced this
  milestone** — every other mapped buffer in this file (frame UBOs, overlay
  VBO, background/grid UBOs) has ridden the same assumption since M4. It is
  named here because the PRECONDITION banner this milestone added to
  `SceneRendererVk.h` enumerates the serialization argument in detail and
  omits the coherence one entirely, so the banner reads as more complete
  than it is. Both arguments would need to hold for the mapped-write
  pattern to be fully justified; only one is currently written down.
- Carried forward unchanged from v1.0.0: no `install()`/`export()` (audit
  gap G4), `Services/PathUtils.h` still declaring `namespace PathUtils` at
  global scope (the open half of audit gap G5), and the parity gate's
  measured detection floor (roughly 0.41–0.68 percentage points of
  high-delta pixels, scene dependent — see v1.0.0's entry for the full
  per-scene breakdown; unchanged and re-confirmed by this milestone's own
  `VkOracleParity` runs, which reproduced those exact numbers to four
  decimal places at every task boundary).
- **The animation UI's layout choices have had no human eye-pass.** The 40px
  clip-selector strip and the 86px transport strip (stacking to 126px when
  both are visible) are sized from ImGui default-metric reasoning, not
  measurement or observation — nobody has run the GUI and looked at them.
  More broadly, the Shell wiring this milestone added has **no automated
  gate at all**: `VkAnimation`/`VkOracleParity` cover the renderer and GPU
  path, but nothing in this suite creates a `Viewport3D`, opens a scene
  through the Shell, and confirms the transport bar or clip selector
  actually draws, responds to input, or looks reasonable.

## v1.0.0 — 2026-08-19

The v1 rewrite closes here: **M1** Target split (`Onyx::Core`/`Render`/
`Shell`), **M2** Identity & state (`TypeRegistrar`, `DiagSink`, `JobQueue`,
`EventBus`, `Settings`), **M3** Modules (`IGameModule`, evidence-ranked
probing, `Workspace`/`Document`, the generic CLI), **M4** Vulkan (the GL
renderer replaced end to end, the parity gate proving it against frozen
goldens), and **M5** Generality (this section) — the exit exam: can a
second toolkit consume the SDK by naming public headers alone. Full
milestone-by-milestone history and gates:
`docs/superpowers/plans/2026-08-18-onyx-v1-roadmap.md`.
M5 shipped as a **public-surface audit** rather than a built second game
module: a toy `comi` module was planned and then deliberately cut
(scope decision, mid-milestone) in favour of four audits — include, link,
capability, and cold-start-TU — over the toolkits the SDK already ships
(`MinimalViewer`, `OnyxBox`, `OnyxCli`), producing the same ranked gaps
list a built module would have. That is weaker in one specific way, worth
stating plainly at this tag: an audit proves the surface is *reachable*;
a second real toolkit would have proven it *sufficient*. Gap by gap, of the
six it found: **G1** (blocking) fixed, **G2** and **G3** (forcing a
workaround) fixed, **G4** not fixed but resolved as a documented scope
decision — there is still no `install()`/`export()` in this tree — **G5**
half fixed, with the `PathUtils` global-namespace half confirmed still
open, and **G6** fixed for every SDK target, leaving only one
example-local library name that has no `Onyx::` alias to move to. So one
gap carries past v1.0 unfixed and one carries half-fixed, both cosmetic,
both named in "Known gaps" below.
**Two human gates are recorded as PENDING, not satisfied by this tag:**
Blender validation of the exported glTF skinned corpus model against a
real DCC tool (`docs/gltf-validation.md` says so explicitly), and the
first push of this repo's history to its remote — which is what would
finally exercise the `linux-lavapipe` CI leg end to end (see "Known gaps"
below and `.github/workflows/ci.yml`'s own header comment).

### Added
- **`Onyx::TestKit`** (`Include/Onyx/TestKit/`, M5) — the SDK ships its own
  opt-in test harness so a second toolkit does not have to reinvent
  golden-tree comparison, decode smoke tests, or render-image comparison:
  `SnapshotTree`/`CompareTreeGolden` (byte-stable hand-built JSON, same
  convention as the oracle's own report and the CLI's `list --json`),
  `DecodeAll` (a Scene/Image/Text decode-everything smoke pass matching
  the CLI's decoder priority and the GUI's Failed-node salvage rule — a
  parse-time Failed node is skipped and counted in its own `skippedFailed`
  bucket, never double-counted as a decode failure), and `CompareImages`/
  `ReadPng`/`CompareRGBA` (moved verbatim — algorithm and tuning
  untouched, only the namespace changed — from `Tools/OnyxOracle/
  ImageCompare.{h,cpp}`/`PngRead.{h,cpp}`; the oracle itself was
  repointed onto this shared home with goldens proven byte-identical
  three times). One CMake target (`Onyx::TestKit`, links only
  `Onyx::Core`): none of Goldens/DecodeSmoke/RenderCompare needs a GPU
  context or a renderer type, so there is no headless/render split to
  make. Consumed by the SDK's own suite (`OnyxTestKit` ctest entry) and
  covered by both CI jobs' unfiltered `ctest` (no `-R` filter — every
  registered test runs), though see the Linux note below: neither job has
  ever executed on a runner.
- **`Onyx::Exchange`** (`Include/Onyx/Exchange/GltfExport.h`, M5) — glTF
  2.0 export of `SceneData` via cgltf v1.15 (write side), wired to
  `onyxbox-cli decode <container> <entry> --to gltf --out model.gltf`.
  Diffuse/Normal/Occlusion textures map onto glTF's core PBR material
  model with zero repacking; baseColor/metallic factors always export.
  Skinning exports the renderer's own inverse bind matrices verbatim
  (`ObjectData::Joint::bindToJointMat`, the exact field `JointPalette.cpp`'s
  `ComputeJointPalette()` consumes as the inverse bind matrix), so the
  exporter is structurally incapable of describing a skeleton the
  renderer would draw differently — not merely observed to agree. The
  rest-pose TRS hierarchy is a cited, deliberate verbatim duplication of
  `JointPalette.cpp`'s own rotation math, unavoidable under the same
  link-cycle constraint `Onyx::Cli::CmdRender` has (`Onyx_Exchange` links
  `Onyx_Core` only; `JointPalette.cpp` compiles into `Onyx_Render`). The
  export skinning gate matches the renderer's own decision exactly (no
  independent `useBindToJoint` flag that could drift from it). Round-trip
  verified via `cgltf_validate` plus a read-back parity ctest (`OnyxGltf`);
  **Blender validation of a skinned corpus model against a real DCC tool
  is PENDING a human** — `docs/gltf-validation.md` says so explicitly and
  warns against reading the green ctest as that checklist being done.
  `Gloss`/`Emissive`/`Height`/`Scatter`/`Detail`/`EnvMap` are not exported
  this pass (per-role reasoning in the header); `Emissive` in particular
  is deferred because `MaterialDesc` carries no emissive factor to pair
  with the texture, not because the mapping itself is hard.
- **`Onyx::CliRender`** (`Include/Onyx/Cli/Render.h`, M5) — closes the one
  blocking gap the public-surface audit found (G1, below): `CmdRender`
  used to be declared in a public header and ship in **no** library, only
  as `Examples/OnyxCli/Render.cpp` compiled straight into the example
  executable — a consumer following the header got an unresolved external
  and had to copy example source out of `Examples/`. It now ships as a
  real fourth library above the `Core`/`Render` link cycle
  (`Onyx_CliRender`, linking `Onyx_Core` + `Onyx_Render` PUBLIC),
  independently reproduced by a cold-start compile-AND-link of a
  throwaway consumer TU against `-I Include` and exactly the documented
  libraries — executable produced and run, not just compiled.
  `Onyx::Cli::Run`'s argv dispatch now takes `render` through an injected
  `RenderFn` hook (mirroring `SceneExportFn`'s glTF shape) so `Onyx_Core`
  stays Vulkan-free while still routing the command for free. Gains spec
  §11's `--views` (canonical `iso/front/back/left/right/top`, checked
  name-for-name against the CLI's own usage text by a `constexpr` walk,
  not just an array-size `static_assert`) and `--strict` (reads the diag
  sink's `Severity::Error`, not a heuristic).
- **`Onyx::Rendering::RenderToImage`** (M5) — the ready floor's one-call
  render entry point: `RenderToImage(request, rgbaOut, err)` owns a full
  `VkContext` lifetime end to end and names no Vulkan type in its own
  signature; a second overload takes a caller-owned, already-`Init()`'d
  context. `Tools/OnyxOracle`'s corpus renderer and `Onyx::CliRender`'s
  `CmdRender` both refactored onto it — goldens proven byte-identical
  (pixel hash and file bytes, before vs. after) three times. Takes a
  plain, non-Vulkan-flipped projection matrix and applies
  `VulkanProjection()` internally exactly once — a total contract chosen
  because the oracle itself forgot this exact flip once already, for a
  full milestone, before a pixel-vs-GL-golden comparison caught it
  (`Include/Onyx/Rendering/Pipelines.h`'s "Camera convention" note); a
  pre-flipped matrix is rejected by a real runtime check in every build
  configuration, not just a Debug assert, since this entry point's
  audience is callers with zero Vulkan knowledge.
- **The public-surface audit** (`docs/design/2026-08-19-public-surface-
  audit.md`, M5) — the exam T2 ran in place of the cut `comi` module: four
  audits (include, link, capability, cold-start-TU) over the SDK's
  existing example toolkits, refusing its own "all clean" false-pass by
  running the cold-start TU in three variants (a trivial one that
  compiled by accident, a real-work one against the umbrella only that
  failed with 6 errors, and a real-work one with 8 explicit includes that
  compiled clean) — proving every API the audit needed both exists and is
  public, and that only the umbrella header failed to surface it. Found 6
  gaps, 1 blocking (`Onyx::Cli::CmdRender` shipping in no library, G1).
  The blocking gap and both forcing-a-workaround gaps (the umbrella's
  narrow reach, G2; `Version.h` absent from the source tree, G3) are
  fixed in this release — see `Onyx::CliRender` and the umbrella entries
  above. The no-`install()`/`export()` gap (G4) is resolved as a
  documented decision, not a code change (README's "Consuming Onyx",
  above). **Two of the audit's cosmetic gaps are NOT fixed and carry past
  v1.0 — see "Known gaps" below:** `Services/PathUtils.h` still declares
  `namespace PathUtils` at global scope (the other half of G5; only its
  `AssetEntry`/`AssetContainer`-alias half was closed, see "Removed"
  below). G6 (`Examples/**` linking raw `Onyx_*` names) is fixed for every
  SDK target — see "Changed" below.
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
  those instance extensions) and has a CI job written for it
  (`linux-lavapipe`), but **that workflow has never run**: this history has
  never been pushed to a remote, so no execution of it exists to be green
  or red (`.github/workflows/ci.yml`'s own header says so, and the first
  push is what would exercise it). Nobody has run the GUI against a real
  Linux window in this milestone either — "implemented" is not the same
  claim as "builds", which is not the same claim as "verified", and
  earlier drafts of this entry conflated all three.
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
- **`<Onyx/Onyx.h>` broadened to a stated, testable inclusion rule** (M5,
  audit gap G2) — the umbrella used to reach 49 of 108 public headers
  while its own comment claimed "the full public surface." The rule is
  now explicit and predictive rather than descriptive, and it has two
  clauses, both checkable against the tree. **(1) Linkable:** everything
  a named header declares ships in a target `Onyx::Onyx` itself links
  (`Onyx_Core`/`Onyx_Render`/`Onyx_Shell`). **(2) Required of the
  consumer:** building a toolkit cannot be done without naming something
  it declares — either to drive an umbrella class through its public
  interface (construct, call, receive, or subscribe to an event it fires)
  or to implement what the SDK delegates (an `IGameModule` with its
  decoders, and the composition root that boots the app or the CLI).
  "Required", not merely useful: a header the Shell or renderer calls
  into on its own behalf does not qualify. Applying it pulled in
  `Modules/DecoderRegistry.h`, `Modules/Selection.h`,
  `App/ViewerOpening.h`/`ViewerRouting.h`, `Cli/Commands.h`,
  `Audio/AdpcmDecoder.h`, `Services/ThemeManager.h`/`PathUtils.h`, and
  `Services/EventManager.h`/`Events.h` (`DocumentWindow`/`Viewport3D`,
  both already in the umbrella, post `EventDocumentOpened`/
  `EventAnimationLoaded` through them — withholding the event catalog
  would leave a consumer of the umbrella's own document/viewer pipeline
  unable to subscribe to events it fires). The Vulkan-touching half of
  the renderer and `Viewers/VideoPlayer.h` stay out on a separate,
  explicitly-stated cost ground — see the sibling umbrellas below.
- **The umbrella now declares only what `Onyx::Onyx` links** (M5, final
  review C1/M3) — an earlier cut of the G2 work had `<Onyx/Onyx.h>` name
  `Exchange/GltfExport.h`, `Cli/Render.h` and the three `TestKit/*`
  headers, arguing in its own comment that the declarations were "free"
  without linking those targets. They were not free: a consumer who
  followed "Consuming Onyx" verbatim (`Onyx::Onyx` and nothing else)
  compiled clean and then got `LNK2019` the moment they called one —
  reproduced on `Onyx::Exchange::ExportSceneData` and
  `Onyx::TestKit::SnapshotTree`. That is the audit's blocking gap G1 (a
  public declaration whose symbol ships in no library the consumer
  linked) seen from the umbrella's side, at three targets instead of one.
  The five headers moved to sibling umbrellas that each name the target
  to link — **`<Onyx/Exchange.h>`**, **`<Onyx/CliRender.h>`**,
  **`<Onyx/TestKit.h>`** — following the `<Onyx/Render.h>`/`<Onyx/Media.h>`
  precedent, and clause (1) of the inclusion rule above exists so the
  same defect cannot be reintroduced by argument. Nothing left the public
  surface: every one of those headers is still installed, still standalone
  compilable, and still reachable by its own `#include`. Caught by the
  final whole-branch review before the tag was pushed, so no released
  version ever carried the trap.
- **`Examples/` links the `Onyx::` aliases** (M5, final review, audit gap
  G6) — `Examples/OnyxBox/CMakeLists.txt` and `Examples/OnyxCli/
  CMakeLists.txt` linked raw `Onyx_Core` while README's "Targets" section
  told every other consumer never to name a raw `Onyx_*` target and claimed
  `Examples/` followed that rule. Now they do, for every SDK target. The one
  raw name left, `Onyx_ExampleBox`, is an example-local library rather than
  an SDK target and has no alias to use; the README sentence says so rather
  than an alias being invented to make the old sentence true. In-tree build
  behaviour is identical — an `ALIAS` target and its aliasee are the same
  target to CMake.
- **The cold-start exam compiles AND links** (M5, final review) — the
  checked-in exam TU (`Tools/ColdStart/ColdStartOnyx.cpp`) was
  `#include <Onyx/Onyx.h>` plus `int main(){}`, compiled with `cl /c` and
  never linked. That is the audit's own "Variant A", the variant the audit
  itself caught false-passing, and a compile-only exam is structurally
  incapable of seeing an unresolved external — which is why an umbrella
  declaring API from three unlinked targets survived four review rounds.
  The TU now does Variant B's real work (registers a module with a real
  `RegisterDecoders` body, writes and parses a synthetic container through
  a `Workspace`, resolves a `NodePath`, invokes a decoder, resolves a
  resource path, logs a line), and a new `ColdStartToolkit` target links it
  against **`Onyx::Onyx` and nothing else** — exactly what README tells a
  consumer to link — with the `ColdStart` ctest entry running the result
  (headless, no `SKIP_RETURN_CODE`, must pass on every runner). The raw
  `cl /c` step in CI stays alongside it: only that step can prove the
  header closure needs no `-I Source`/`-I build/generated`, and only the
  link can prove the declarations are callable. It found the `TextOut`
  collision above on its first run.
- **Every logging macro is `ONYX_`-prefixed** (M5, final review) —
  `Services/Logger.h` is reachable from `<Onyx/Onyx.h>`, so until now every
  consumer of the umbrella inherited four unprefixed global macros
  (`LOG_DEBUG`/`LOG_INFO`/`LOG_WARN`/`LOG_ERR` — among the most commonly
  `#define`d identifiers in C++, claimed by glog, plog, easylogging and most
  engine loggers) plus a `GOW_LOG_*` family marked "preferred", i.e. a God
  of War name on the main logging API of an SDK whose premise is that it
  knows nothing about any specific game (v1 spec §13, in the one place §13's
  letter did not reach). Now: `ONYX_LOG_TRACE/DEBUG/INFO/WARN/ERROR(cat,
  fmt, ...)` for the category-aware set, `ONYX_LOGF_DEBUG/INFO/WARN/ERR(fmt,
  ...)` for the printf-style one. The short spellings still exist but only
  behind `#define ONYX_LEGACY_LOG_MACROS` — **opt-in, never on by default**,
  so Onyx can no longer silently capture a consumer's own `LOG_INFO`.
  ~150 mechanical edits today; a 2.0.0 bump if it had shipped.
- **`Onyx::Modules::TextOut` renamed to `DecodedText`** (M5, final review) —
  `TextOut` is a macro on Windows (`<wingdi.h>` defines it as
  `TextOutA`/`TextOutW`), and `<Onyx/Onyx.h>` reaches `<windows.h>` through
  `Services/PathUtils.h`. A consumer of the umbrella therefore declared
  `Onyx::Modules::TextOutA` in their own TU while `Onyx_Core` shipped
  `Onyx::Modules::TextOut` — an `LNK2019` on
  `DecoderRegistry::Text`/`DecodeText` for anyone reaching the decoder
  contract the documented way. Nothing in the SDK's own sources hit it,
  because no in-tree TU included `PathUtils.h` and called a text decoder in
  the same file. Found by the new cold-start link exam below on its first
  run. The type name changes; the field names (`text`, `language`) and every
  signature shape do not. A rename here after the tag would have been
  MAJOR-class under this release's own stability policy.
- **`<Onyx/Render.h>` and `<Onyx/Media.h>`** (M5, audit gap G2) — sibling
  umbrellas for the two halves `<Onyx/Onyx.h>` excludes on cost grounds
  rather than link grounds: the 7 Vulkan-touching renderer headers (every
  one pulls in `volk.h`/`vk_mem_alloc.h` directly or transitively) and
  `Viewers/VideoPlayer.h` (the one viewer whose *header* directly includes
  FFmpeg/miniaudio, which would break `#include <Onyx/Onyx.h>` itself when
  `ONYX_COMPONENT_MEDIA` is off). Each documents its own third-party
  dependency list in its top comment; `Render.h`'s list was found
  incomplete (`AxisGizmo.h` silently needs `imgui.h`) and corrected — the
  underlying design smell (a Vulkan-rendering target's public header with
  a hard ImGui dependency) is flagged, deliberately unfixed, in "Known
  gaps" below.
- **`Include/Onyx/Version.h` checked into the source tree** (M5, audit gap
  G3) — previously generated only into `build/generated/Onyx/Version.h`
  and never present for a consumer who had cloned the repo but not yet
  run CMake, even though it is `<Onyx/Onyx.h>`'s first include.
  `configure_file()` now writes directly into `Include/Onyx/Version.h`
  (checked in) instead; a cold-start compile of the umbrella needs no
  `-I build/generated` any more.
- **Configure removes a stale `build/generated/Onyx/Version.h`** (M5, final
  review) — `fbd54d3` moved the generated version header into the source
  tree (G3, above) and nothing deleted the old output, so any build
  directory created before that commit keeps a `Version.h` frozen at
  whatever version it last configured (0.6.0 in this repo's own build
  directory, found at the final review) and it never self-heals, because
  configure no longer writes there. Both `Include` and
  `${CMAKE_CURRENT_BINARY_DIR}/generated` sit on every Onyx target's PUBLIC
  include path with `Include` first, so nothing in-tree ever read the stale
  copy — but a consumer whose own target lists the generated directory
  first, or who followed pre-`fbd54d3` guidance, silently got a 1.0.0 SDK
  reporting 0.6.0. Configure now `file(REMOVE)`s exactly that one path.
- **Consumption model documented, not built** (M5, audit gap G4) — README's
  "Consuming Onyx" section now states plainly, up front, that
  `FetchContent`/`add_subdirectory` against `Onyx::*` targets is the
  supported v1 model and that `install()`/`export()`/`find_package()` are
  not supported and not a v1 promise, rather than silently absent. Chosen
  over building real install/export support: nine targets is real,
  untested packaging surface to add on top of an already-invasive
  milestone, and the audit ranked this "forces a workaround," not
  blocking, since vendoring via `add_subdirectory()` demonstrably works
  today.
- **Stability policy documented** (v1 spec §15, README's "Stability
  policy") — public surface = `Include/Onyx/**` minus `Detail/`; the
  promise is source compatibility only (API, not ABI — Onyx ships as
  source, every consumer recompiles). As of this tag the project is
  post-1.0: PATCH is bug-fix-only, MINOR is additive-only, MAJOR is the
  only class allowed to remove/rename/reshape a public declaration. A
  header moving between two public locations (the `RenderVk` → `Rendering`
  fold below is the worked example) is MAJOR-class from this tag forward.
- **`Include/Onyx/RenderVk/` folded into `Include/Onyx/Rendering/`** (M5,
  part of the G2 umbrella work) — all 7 headers, all 8 `.cpp`/8 shader
  files moved (`git mv`, zero content diff on the move itself); every
  `#include` site and the shader-compile CMake list repointed. Proven
  mechanical: `git diff --stat -- Tests/Golden` empty, 52/52 green,
  parity + reproducibility 3× stable throughout. Eight stale
  `Onyx/RenderVk/` path references survived the move in 6 shader-file
  comments plus `ci.yml`, fixed separately.
- Global-scope `GLuint` typedef and remaining GOW-named comments removed
  from the Render layer (v1 spec §13 — a layer a consumer compiles against
  should describe mechanisms, not game names): `RenderBatch.h`'s
  `using GLuint = unsigned int` replaced tree-wide with plain `uint32_t`
  (same width, no ABI change); `RenderBatch.h`/`JointPalette.h`/
  `Rendering/SceneRendererVk.cpp` comments rewritten in mechanism terms
  ("a role-suffixed texture naming convention," "a per-game bind-pose
  orientation convention") instead of naming GOWR/GOW2 directly; an unread
  `TextureData.h` field (`glInternalFormat`, zero readers tree-wide)
  removed rather than renamed.
- The CLI's canonical-view name check (`kCanonicalViews`/`kViewAngles`)
  now compares names, not just array size — a `constexpr`
  `ViewNamesMatchCanonical()` walk replaces a `static_assert` that only
  caught a length mismatch, so a rename in one list that left the other
  stale used to compile clean and only surface at runtime as "unknown
  view" for a name the usage text still advertised.
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
- **BREAKING (M5 T8 fix round, spec §15/G5):** every global-scope
  backwards-compat alias for `AssetEntry`/`AssetContainer` is gone —
  `Include/Onyx/Domain/Entry.h`, `Include/Onyx/Domain/Wad.h`,
  `Include/Onyx/Services/AssetVisibility.h`, and `Include/Onyx/Types/
  ITypeHandler.h` each declared their own `using AssetEntry = ...`/
  `using AssetContainer = ...` at global scope independently (three
  copies of the `AssetEntry` alias, two of `AssetContainer`) — use
  `Onyx::Domain::AssetEntry`/`Onyx::Domain::AssetContainer` (or
  `Domain::AssetEntry`/`Domain::AssetContainer` from inside any
  `Onyx::*` namespace) instead. G5 is fully closed: no global-scope
  alias for either type remains anywhere in the public headers.
  Removing them now is a MINOR bump; the same fix after v1.0.0 would
  need a MAJOR one for a pure namespace-hygiene change.

### Known gaps — the v1.0 promise
Carried forward from M4 into this tag on purpose, corrected where M5 found
the M4 text wrong, and extended with what M5 itself found and left
unfixed — so nobody rediscovers any of it by surprise once the milestones'
working ledgers (`.superpowers/sdd/2026-08-19-onyx-v1-m4-vulkan/`,
`.superpowers/sdd/2026-08-19-onyx-v1-m5-generality/`) are gone at merge.
None of these are silent: every one is either disabled with an in-UI
tooltip, documented in the source at the exact spot a reader would look,
or both.
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
  four-tier tolerance (see above) catches a new defect only once it adds
  roughly **0.41 to 0.68 percentage points** of pixels at delta>8, scene
  dependent, or is broad enough to move whole-image MAE past 1 LSB. That
  range is *measured*, not estimated: the `highDeltaPct` cap is 0.8% and
  the gate prints every scene's own baseline on every run
  (`ctest -R VkOracleParity -V`, real AMD hardware, at this tag) —
  `sphere-grid-textured` 0.3941% (0.406 points of headroom, the tightest),
  `sphere-grid` 0.3922% (0.408), `joint-chain-200` 0.2220% (0.578),
  `skinned-cube` 0.1549% (0.645), `blend-stack` 0.1225% (0.678). An earlier
  version of this entry claimed 0.2-0.3%, roughly twice as sensitive as the
  gate actually is, with no derivation recorded anywhere; it looks carried
  over from M4 and never re-measured after the four-knob retune. The other
  two tiers are further from binding still: worst `differingPct` is 1.4748%
  against a 2.0% cap, worst `mae` 0.1423 against 1.0. Re-measure these
  numbers, do not copy them, if the thresholds or the corpus ever move.
  The gate does **not** catch a defect confined to one small object's
  silhouette (~0.13% of the frame in the adjudicated measurements) — that
  is further below the floor than the old text implied, and real teeth
  against that class of regression would come from the opt-in per-scene
  metrics ratchet `compare --emit-metrics` already emits into every ctest
  log, not yet wired into any gate.
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
  **Correction (M5 T7):** M4's own text elsewhere claimed `Viewport3D`
  was one of "three consumers" duplicating roughly 60 lines each that
  `RenderToImage` (above) would collapse. That is overstated and is not
  repeated here: `Viewport3D` was never actually a `RenderToImage`
  candidate — it holds a persistent `OffscreenTarget` created once per
  resize (not per frame), builds its pipelines once, exposes the result
  as a live GPU texture via `TexturePool::RegisterExternalView`, runs up
  to four passes, and does zero CPU readback, none of which fits a
  one-shot scene-render-to-RGBA-bytes contract. Forcing the fit would
  have added a CPU round-trip to an interactive view to satisfy a refactor
  metric. Of the ~60 lines, roughly 15-20 were literal duplication; the
  rest is structural difference between an interactive live-texture
  viewer and a one-shot offscreen renderer, and does not go away by
  sharing a function.
- **`RenderRequest` (the struct `RenderToImage` takes) grew background
  fields to keep the render-corpus oracle's own gradient background — and
  therefore the parity gate itself — intact when the oracle moved onto
  the new entry point.** Flagged as a grow-one-field-per-consumer pattern
  that will not scale: if a second caller needs a differently-shaped
  pre-scene pass, the principled fix is a `RenderContext::AddPass`-shaped
  hook on the request, not a third bespoke field. Not built now because
  there is exactly one consumer that needs it today.
- **`Tools/OnyxOracle`'s corpus renderer still calls `Build()` a second
  time per scene, pixel-inert, only to recover `GetBatches()` for the
  JSON report** — a full duplicate GPU upload that does not affect any
  pixel or golden. An optional `outBatches` parameter on `RenderToImage`
  would remove it; premature for the one consumer that has this need.
- **`<Onyx/Render.h>` has a hard ImGui dependency it should not need.**
  `AxisGizmo.h` (pulled into the umbrella above) draws its gizmo discs
  through `ImDrawList`, which means `Onyx::Render` — a Vulkan-rendering
  target with no UI concerns of its own — cannot be compiled without a UI
  library on the include path. Found and documented (M5 T8), deliberately
  not restructured this milestone. Candidate v1.1 fix: move `AxisGizmo`
  into `Onyx::Shell` (it already draws through ImGui, not Vulkan, so it
  arguably belongs there architecturally) or split `Render.h` into a
  truly headless slice plus an opt-in gizmo/debug-draw header.
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
- **`Services/PathUtils.h` still declares `namespace PathUtils` at global
  scope** (the audit's G5, other half — see "Removed" above for the half
  that was fixed). Every consumer of it sees a bare `::PathUtils::...`
  rather than something nested under `Onyx::`. Cosmetic, audit-ranked, not
  touched this milestone; carries past v1.0, so fixing it later is a
  MAJOR-class rename under this tag's own stability policy. **The same
  header also `#include`s `<windows.h>` unconditionally on Windows**, at
  global scope, in a file `<Onyx/Onyx.h>` includes — with no `#ifndef`
  guard on its own `WIN32_LEAN_AND_MEAN` define and no `NOMINMAX` of its
  own (`onyx_apply_common()` sets `NOMINMAX` PUBLIC, so anyone linking an
  Onyx target inherits it; anyone including the header without linking
  does not). This is not purely cosmetic: it is how `<wingdi.h>`'s `TextOut`
  macro reached the decoder contract and broke the umbrella's link promise
  (see `DecodedText` under "Changed"). The rename closed that instance; the
  leak that carried it is still here. Post-v1: give the header a
  platform-shim of its own, or forward-declare the two Win32 entry points
  it actually uses instead of pulling the whole header in.
- **Five public headers use quoted includes for a third-party header.**
  `App/App.h`, `App/TypeVisuals.h`, `App/Widgets.h`, `Services/Appearance.h`
  and `Services/ThemeManager.h` all `#include "imgui.h"` rather than
  `<imgui.h>`. It works (the cold-start exam compiles them), but the quoted
  form searches the includer's own directory first and is the wrong signal
  for a third-party header. Cosmetic; post-v1.
- **`Include/Onyx/Cli/Gltf.h` declares a symbol that ships in no library.**
  `MakeGltfExportFn` is compiled into `Examples/OnyxCli`'s own executable,
  so a consumer who includes this public header gets an unresolved external
  no target can satisfy — structurally the audit's G1 again, in the one
  place it was not closed. It is deliberately NOT named by `<Onyx/Onyx.h>`
  or any sibling umbrella, so nothing hands it to a consumer by accident,
  and the header's own top comment explains the link-cycle reasoning behind
  the placement. Post-v1 fix: either ship the composition-root helper in a
  real target, or move the header out of `Include/`.
- **`Tools/OnyxOracle/CMakeLists.txt` names an AI model in a shipped build
  file** ("Adjudication (opus) POSITIVELY identified..."). Pre-existing from
  M4, outside this milestone's diff, and one word wide; the repo's standing
  no-AI-attribution rule says it should go. Post-v1 cleanup, deliberately
  not swept into this tag's diff.
- **`Examples/**` still names one raw `Onyx_*` target: `Onyx_ExampleBox`**
  (the remainder of audit G6). Every SDK target the examples link is now
  spelled `Onyx::` (fixed at the final review, see "Changed"), but
  `Onyx_ExampleBox` is defined in `Examples/OnyxBox/` — an example-local
  library, not part of the SDK — and has no `Onyx::` alias. Inventing one
  purely to satisfy a README sentence would advertise an SDK target that
  does not exist, so the sentence says what is true instead. Nothing here
  breaks under a future `install()`/`export()` (G4), which would export SDK
  targets only.

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
