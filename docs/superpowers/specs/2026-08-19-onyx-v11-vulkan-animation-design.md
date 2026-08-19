# Onyx v1.1.0 — Skeletal animation on Vulkan, plus the two trivial gaps

Status: DESIGN — awaiting approval
Target tag: v1.1.0 (MINOR: purely additive to the public surface)
Predecessor: v1.0.0 (`32517cc`), whose "Known gaps" section this closes in part

## Why this exists, and why it comes before the toolkit port

GoWToolkit is still pinned to `v0.6.0`. The v1 port cannot start until the
renderer it ports onto can do what the GL renderer did, because the toolkit's
whole reason to exist is inspecting God of War assets — and a character asset
that renders only in rest pose is an asset half-inspected. v1.0.0's own
"Known gaps" states the loss plainly:

- no animation playback (`SceneRendererVk` has no `SetAnimation`/
  `UpdateAnimation`; `App::GetActiveAnimationPlayer()` is permanently null,
  so `Dopesheet` and `AnimCurveView` degrade to showing nothing)
- no per-batch visibility culling (`Render()` never reads
  `RenderBatch::isVisible`, so the Inspector's checkboxes do nothing)
- three viewport color pickers disabled with a tooltip because nothing
  reads `AppConfig::boneR`/`wireR`/`hlR`

This milestone closes the first, and the two cheap ones riding along with it.
Outline/hover-highlight and wireframe stay open and stay declared — see
"Out of scope".

## What already exists (and therefore is not the work)

The evaluator is not missing. `Onyx::Rendering::AnimationPlayer`
(`Source/Rendering/AnimationPlayer.cpp`) compiles today, bakes clips, scrubs,
loops/ping-pongs, and hands back finished joint matrices through
`ComputeJointMatrices()`. `JointPalette.cpp` has the shared rest-pose math and
`BuildBatchPalette(jointPalette, jointMap)` already remaps a full palette down
to one batch's joint subset. `Dopesheet` and `AnimCurveView` are written and
wired — they just read a null pointer.

What is missing is the plumbing between them: nothing constructs a player,
nothing calls `SetActiveAnimationPlayer`, and the GPU-side palette is written
exactly once.

## The one real design decision: how the palette buffer becomes dynamic

Today (`SceneRendererVk.cpp:407`) each batch's joint palette is a
`VMA_MEMORY_USAGE_GPU_ONLY` SSBO filled by a staging `Resources::Upload` inside
`Build()` — a one-shot write of the rest pose. Animation needs it rewritten
every frame the pose changes.

**Decision: the joint SSBO becomes `VMA_MEMORY_USAGE_CPU_TO_GPU`,
persistently mapped, written through the same `WriteMapped()` helper the file
already uses.** No double-buffering, no new barrier, no staging path per frame.

This is safe by construction, not by hope, and the reason is worth writing
down because it is the fact that makes the whole design cheap:
**scene rendering is fully serialized.** Every scene submission goes through
`Resources::OneShot`, which submits and then blocks on `vkWaitForFences`
(`VkResources.cpp:424`) before returning — `Viewport3D::RenderFrame`
(`Viewport3D.cpp:359`), the oracle, and `RenderToImage` all take that path.
`kFramesInFlight = 2` belongs to the ImGui swapchain in `Window.cpp`, which
only samples the already-resolved offscreen texture; it never has a scene
command buffer in flight. So the CPU cannot be writing a palette the GPU is
still reading.

It is also the convention this file already follows for every buffer it
rewrites per frame: the main and sky frame UBOs, the overlay VBO, the
background UBO and the grid UBO are all created `CPU_TO_GPU`, mapped once at
creation, `pMappedData` cached, and validated non-null with an error string at
`Build()` time. The palette joins that list rather than inventing a second
mechanism beside it.

Cost: the palette moves from device-local to host-visible memory. Sizing —
`sizeof(mat4) * jointCount` per batch, so a 200-joint skeleton across 20
batches is ~256 KB total. That is not a budget worth a staging ring.

**If serialization ever ends** — someone builds a real in-flight scene path —
this decision breaks, and so does the frame UBO's, identically and at the same
moment. The invariant is therefore recorded in `SceneRendererVk.h`'s top
comment as a named precondition of the class, not buried at the allocation
site, so the person who removes `OneShot` finds it.

## Public API

`SceneRendererVk` gains the GL renderer's animation surface, **name for name**
as it stood at `d4551de^` — the toolkit's call sites then port mechanically
instead of being rewritten:

```cpp
bool  HasAnimations() const;
const Parsers::AnimationData* GetAnimationData() const;
AnimationPlayer* GetAnimPlayer();          // null until SetAnimation

void  SetAnimation(int groupIdx, int actIdx);
void  StopAnimation();
bool  UpdateAnimation(float dt);           // true => pose changed, redraw
```

`Build()` keeps ownership of the `AnimationData` the scene carried, exactly as
GL's did.

### Two deliberate departures from the GL original

1. **The rest pose survives.** GL did `m_jointPalette = std::move(animatedMats)`,
   overwriting the only copy it had — after which `StopAnimation()` could not
   restore the bind pose without a full `Build()`. Here `Build()` keeps
   `m_restPalette`/`m_restJointWorldPos` immutable and animation writes a
   separate `m_jointPalette`; `StopAnimation()` copies rest back, reuploads,
   and the viewport returns to the pose it started in.
2. **The `[PalDiag]` block does not come back.** GL logged pelvis translation
   and quaternion every 120th call from inside the hot path. It was scaffolding
   for a bug that is long fixed.

## Behaviour

`UpdateAnimation(dt)` keeps GL's two-branch shape, because the second branch is
load-bearing and non-obvious: when the player is *paused*, the UI can still move
the pose through `SetTime`/`SetFrame` (that is what scrubbing the Dopesheet
does), and `Update()` never runs. So the paused branch compares the player's
time against `m_lastAppliedAnimTime` and repaints on a mismatch. Dropping it
would make scrubbing silently dead while playback worked — the exact defect
shape that survives review.

When the pose changed: `ComputeJointMatrices()` → `m_jointPalette` →
`m_jointWorldPos[i] = m_jointPalette[i][3]` (so the skeleton overlay follows
the animation, which GL also did) → for each batch, `BuildBatchPalette` into
its mapped SSBO. Unskinned batches hold a one-entry identity palette and are
skipped.

## The two trivial gaps

- **Culling.** `Render()` skips `!batch.isVisible`, matching GL's
  `if (!batch->gpuMesh || !batch->isVisible) continue;`. The Inspector
  checkboxes start working. `isHighlighted` stays unread — that needs the
  outline pass, which is out of scope, so it stays a declared gap rather than
  quietly half-wired.
- **Bone color.** The skeleton overlay reads `AppConfig::boneR/G/B` instead of
  its hardcoded color; the Settings picker loses its disabled state and
  tooltip. Wireframe and outline pickers stay disabled — their passes still
  do not exist.

## Shell wiring

- `Viewport3D::Draw()` calls `UpdateAnimation(dt)` at the spot that today holds
  the comment "Camera flight animation only (mesh animation has no Vulkan API)"
  and requests a repaint when it returns true.
- Whoever owns the live `SceneRendererVk` calls
  `App::SetActiveAnimationPlayer(renderer.GetAnimPlayer())` on scene load and
  `nullptr` on teardown. `Dopesheet` and `AnimCurveView` then light up with no
  change to their own code — they already handle a valid player.
- The transport bar and clip browser removed at M4 T11 come back, recovered
  from history rather than rewritten. Their exact deletion commit is a task-time
  lookup, not a design question.

## Gates

1. **`VkOracleParity` stays green, unmoved.** Rest pose must be byte-identical
   after the buffer's memory type changes. This is the gate that proves the
   `GPU_ONLY` → `CPU_TO_GPU` move is a no-op for everything that exists today,
   and it runs against frozen GL goldens, so it cannot be argued with.
2. **A new `VkAnimation` ctest, self-contained — no new golden file.** The
   corpus carries no `AnimationData` at all (verified: `CorpusScenes.cpp` builds
   skeletons and skin weights, zero clips), so this gate brings its own
   `BuildAnimatedChain()` scene, deliberately **not** added to `BuildCorpus()` —
   adding it there would move the parity corpus and force new goldens, which
   gate 1 exists to forbid. The gate renders that one scene four times in a
   single process and compares the images against each other:

   - (a) built, never animated — the reference
   - (b) `SetAnimation` then `UpdateAnimation(0.0f)` — must match (a)
   - (c) advanced to mid-clip — must **differ** from (a), far above the
     comparison's noise floor. A test that only asserted "it still renders"
     would pass against today's renderer, which ignores animation entirely.
   - (d) `StopAnimation()` — must match (a) again. Guards departure (1).

   (a)-vs-(b) and (a)-vs-(d) are compared with `TestKit::CompareImages`'
   tolerance rather than asserted byte-identical, and the reason is specific:
   the rest palette comes from `ComputeJointPalette()` (reading `vectors4/5/6`
   directly) while the animated one comes from
   `AnimationPlayer::ComputeJointMatrices()` (reading `m_jointLocalRot/Pos/Scale`,
   which `Reset()` copies out of those same vectors). Same TRS chain, same
   inputs, but not provably the same float operation order — demanding
   bit-equality across two code paths would be a gate that fails on an ULP and
   teaches nothing.
3. **Scrub without play.** `SetTime()` on a paused player, then
   `UpdateAnimation(0.0f)` returns true and the image moves. Guards the paused
   branch above.
5. Full `ctest` unfiltered, both CI legs, as v1.0.0 established.

## Out of scope, and staying declared

Outline/hover-highlight pass, wireframe mode, matcap (alias-only on GL too —
it silently drew Solid), and the wireframe/outline color pickers. All four
stay in the CHANGELOG's "Known gaps" with their text updated to say animation
is no longer among them.

`install()`/`export()` (G4) and `PathUtils` at global scope (G5's open half)
are untouched — they carried past v1.0 as documented decisions and this
milestone does not reopen them.

## Risk

The buffer memory-type change touches every skinned scene the parity gate
covers, and gate 1 is what catches it. The rest is additive: a renderer that
never has `SetAnimation` called on it takes exactly the path it takes today.
