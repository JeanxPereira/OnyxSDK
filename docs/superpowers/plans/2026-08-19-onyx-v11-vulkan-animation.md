# Onyx v1.1.0 — Vulkan Skeletal Animation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give `SceneRendererVk` the GL renderer's animation surface so skinned assets play their clips again, and close the two trivial gaps (per-batch visibility culling, bone color from config) riding along with it.

**Architecture:** The evaluator (`AnimationPlayer`) and the palette math (`JointPalette.cpp`) already exist and are untouched. The work is plumbing: the per-batch joint SSBO moves from a device-local one-shot upload to a persistently-mapped host-visible buffer (safe because every scene submission blocks on a fence inside `Resources::OneShot`), the renderer grows `SetAnimation`/`StopAnimation`/`UpdateAnimation` mirroring the deleted GL names, and the Shell's transport bar and clip browser are recovered verbatim from history.

**Tech Stack:** C++20, Vulkan 1.3 (volk + VMA), doctest, CTest, Ninja. Build dir `build/` is the `debug` preset, already configured with `ONYX_BUILD_TESTS=ON` and `ONYX_BUILD_EXAMPLES=ON`.

**Spec:** `docs/superpowers/specs/2026-08-19-onyx-v11-vulkan-animation-design.md`

## Global Constraints

- **`VkOracleParity` must stay green and byte-identical throughout.** It renders the 5-scene corpus against frozen GL goldens and is the only thing proving the buffer change is a no-op. Never regenerate a golden to make it pass.
- **Do not add any scene to `BuildCorpus()`.** That function's output order and content are what the goldens pin. The new animated scene is a separate builder the parity gate never sees.
- **Logging macros are `ONYX_LOG_*` / `ONYX_LOGF_*`.** The short `LOG_*` spellings only exist behind `ONYX_LEGACY_LOG_MACROS` and must not be introduced.
- **`Onyx_Render` links no GL and names no GL type.** Palette handles stay `uint32_t`.
- **No new public header may declare a symbol that ships in no library** (v1.0.0's audit gap G1). Everything added here lands in `Onyx_Render`, which `Onyx::Onyx` already links.
- Build: `cmake --build build`. Tests: `ctest --test-dir build -R <name> -V`.
- Commit style: Conventional Commits, sentence-case subject, no attribution trailers.

---

### Task 1: A synthetic animated scene, proven without a GPU

The corpus has no `AnimationData` anywhere, so every later gate needs a clip to
play. This task builds one and proves it bakes and moves the pose — entirely on
the CPU, in doctest, with no Vulkan device involved. If this is wrong,
everything downstream tests nothing.

**Files:**
- Modify: `Tools/OnyxOracle/CorpusScenes.h` (declare `BuildAnimatedChain`)
- Modify: `Tools/OnyxOracle/CorpusScenes.cpp` (implement it, after `BuildSkinnedCube`)
- Create: `Tests/animclip_test.cpp`
- Modify: `Tests/CMakeLists.txt` (add the source + an `OnyxAnimClip` test entry)

**Interfaces:**
- Consumes: `Onyx::Parsers::{AnimationData, AnimGroup, AnimAct, AnimStateDescr, SkinningState, AnimSubstream, AnimSamplesManager, AnimDataType, ANIM_DATATYPE_SKINNING}`, `Onyx::Rendering::AnimationPlayer`, the existing file-local `EncodeEulerDegreesQ14()` in `CorpusScenes.cpp`.
- Produces: `Onyx::OracleTool::CorpusScene BuildAnimatedChain();` — a `BuildSkinnedCube()` clone whose `scene.animations` holds one group (`"test"`) with one act (`"bend"`), duration 1.0s, bending joints 1 and 2 from 0° to 60° around X across 31 samples at 1/30s. Tasks 4 and 5 render it.

**Background the implementer needs (do not rediscover this):**

`AnimSubstream::samples` is keyed `jointId * 4 + coordIdx` and, when
`isAdditive == false`, `HandleSkinningStream` writes the sample **absolutely**
(`jointData[jointId][coord] = value`) with linear interpolation between
neighbours — so a hand-written clip is just a list of per-frame values, no
delta accumulation to reason about.

Rotation values are in the same Q.14 encoded-degrees units as
`ObjectData::vectors5`, because `AnimationPlayer::Reset()` seeds
`m_jointLocalRot` straight from `vectors5`. `CorpusScenes.cpp` already has
`EncodeEulerDegreesQ14()` — use it, do not hand-compute the constant.

`ReturnStreamDataIndex` with `offset = 0` and `datasCount3 = 0` reduces to
`sampleIndex = animTime / frameTime`, valid while that is `<= count - 1`. So
`count = 31` at `frameTime = 1/30` covers exactly `duration = 1.0`.

`EnsureBaked()` captures frame 0 **before** the stream walk begins (it calls
`Reset()` then `capture(0)`, and the loop starts at `f = 1`). Frame 0 is
therefore the skeleton's rest pose, and `samples[0]` is never read. This is
deliberate and is what makes gate (b) in the spec — "animation set, not
advanced, looks unchanged" — true by construction rather than by luck. Do not
"fix" it.

- [ ] **Step 1: Write the failing test**

Create `Tests/animclip_test.cpp`:

```cpp
// Pure, no-device tests for the synthetic animated corpus scene (Task 1 of
// the v1.1 animation milestone). Nothing here touches Vulkan: it drives
// AnimationPlayer directly over BuildAnimatedChain()'s hand-built clip, so
// a failure here means the clip or the bake is wrong, never the renderer.
#include <doctest/doctest.h>

#include <Onyx/Rendering/AnimationPlayer.h>
#include "CorpusScenes.h"

#include <cmath>

namespace {

// Largest absolute difference between two joint-local rotation sets, in the
// Q.14 encoded-degree units vectors5/m_jointLocalRot both carry.
float MaxRotDelta(const std::vector<glm::vec4>& a, const std::vector<glm::vec4>& b) {
    float worst = 0.0f;
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < 4; ++c)
            worst = std::max(worst, std::fabs(a[i][c] - b[i][c]));
    return worst;
}

} // namespace

TEST_CASE("AnimClip: BuildAnimatedChain carries one skinning act with a baked clip") {
    auto cs = Onyx::OracleTool::BuildAnimatedChain();

    REQUIRE(cs.scene.animations != nullptr);
    CHECK(cs.scene.animations->FindSkinningTypeIndex() == 0);
    REQUIRE(cs.scene.animations->groups.size() == 1);
    REQUIRE(cs.scene.animations->groups[0].acts.size() == 1);
    CHECK(cs.scene.animations->groups[0].acts[0].duration == doctest::Approx(1.0f));
    REQUIRE(cs.scene.skeleton != nullptr);

    Onyx::Rendering::AnimationPlayer player;
    player.SetAnimation(cs.scene.animations.get(), 0, 0, cs.scene.skeleton.get());

    REQUIRE(player.GetBaked() != nullptr);
    CHECK(player.GetFrameCount() == 31);
    CHECK(player.GetCurrentActName() == "bend");
    CHECK(player.GetDuration() == doctest::Approx(1.0f));
}

TEST_CASE("AnimClip: t=0 holds the rest pose and mid-clip moves away from it") {
    auto cs = Onyx::OracleTool::BuildAnimatedChain();

    Onyx::Rendering::AnimationPlayer player;
    player.SetAnimation(cs.scene.animations.get(), 0, 0, cs.scene.skeleton.get());

    // The bake captures frame 0 before walking any stream, so t=0 is the
    // skeleton's own rest pose -- this is what lets the render gate assert
    // "animation set but not advanced changes nothing".
    player.SetTime(0.0f);
    const std::vector<glm::vec4> atZero = player.GetJointRotations();
    REQUIRE(atZero.size() == cs.scene.skeleton->joints.size());
    for (size_t i = 0; i < atZero.size(); ++i)
        CHECK(atZero[i].x == doctest::Approx(float(cs.scene.skeleton->vectors5[i].x)));

    player.SetTime(0.5f);
    const std::vector<glm::vec4> atMid = player.GetJointRotations();

    // Joint 0 is the unbent root and is never keyed; joints 1 and 2 are.
    CHECK(atMid[0].x == doctest::Approx(atZero[0].x));
    CHECK(MaxRotDelta(atZero, atMid) > 1.0f);
}

TEST_CASE("AnimClip: Stop returns the player to the rest pose") {
    auto cs = Onyx::OracleTool::BuildAnimatedChain();

    Onyx::Rendering::AnimationPlayer player;
    player.SetAnimation(cs.scene.animations.get(), 0, 0, cs.scene.skeleton.get());
    player.SetTime(0.0f);
    const std::vector<glm::vec4> rest = player.GetJointRotations();

    player.SetTime(0.5f);
    REQUIRE(MaxRotDelta(rest, player.GetJointRotations()) > 1.0f);

    player.Stop();
    CHECK(player.IsPlaying() == false);
    CHECK(MaxRotDelta(rest, player.GetJointRotations()) == doctest::Approx(0.0f));
}
```

- [ ] **Step 2: Wire the test into the build**

In `Tests/CMakeLists.txt`, add `animclip_test.cpp` to the `onyx_tests` source
list (alongside `oracle_corpus_test.cpp`, which already proves `CorpusScenes`
is reachable from this target), and register the entry next to
`OnyxOracleCorpus`:

```cmake
add_test(NAME OnyxAnimClip        COMMAND onyx_tests "--test-case=*AnimClip:*")
```

- [ ] **Step 3: Run it and watch it fail**

```bash
cmake --build build 2>&1 | tail -20
```

Expected: compile error, `BuildAnimatedChain` is not a member of
`Onyx::OracleTool`. That is the correct failure — the declaration comes next.

- [ ] **Step 4: Declare the builder**

In `Tools/OnyxOracle/CorpusScenes.h`, below the four existing builders, add:

```cpp
// Not part of BuildCorpus() on purpose: the parity goldens pin BuildCorpus()'s
// exact output, so a sixth scene there would force a golden regeneration --
// which the milestone's first gate exists to forbid. This one is built only by
// the animation gate (onyx-oracle --vk-animation-smoke) and by Tests/animclip_test.cpp.
CorpusScene BuildAnimatedChain(); // BuildSkinnedCube + a 1s bend clip on joints 1,2
```

- [ ] **Step 5: Implement the builder**

In `Tools/OnyxOracle/CorpusScenes.cpp`, immediately after `BuildSkinnedCube()`:

```cpp
// ── animated-chain ───────────────────────────────────────────────────────
//
// BuildSkinnedCube's geometry and skeleton verbatim, plus the one thing the
// whole corpus lacks: an actual clip. Joints 1 and 2 sweep 0deg -> 60deg
// around X over 1.0s at 1/30s, written as absolute (non-additive) samples,
// which is the branch of HandleSkinningStream that assigns rather than
// accumulates -- see that function in Source/Rendering/AnimationPlayer.cpp.
//
// Sample index 0 is never read by the bake (EnsureBaked captures frame 0 from
// the rest pose before its walk starts at f=1), so it is written for
// completeness only.
CorpusScene BuildAnimatedChain() {
    CorpusScene cs = BuildSkinnedCube();
    cs.name = "animated-chain";

    constexpr int   kSampleCount = 31;    // frames 0..30 == 1.0s at 1/30
    constexpr float kFrameTime   = 1.0f / 30.0f;
    constexpr double kBendDegrees = 60.0;

    auto anim = std::make_shared<AnimationData>();
    anim->dataTypes.push_back(AnimDataType{ANIM_DATATYPE_SKINNING, 0, 0});

    AnimSubstream rot;
    rot.isAdditive = false;
    rot.manager.count        = kSampleCount;
    rot.manager.offset       = 0;
    rot.manager.datasCount3  = 0;
    rot.manager.offsetToData = 0;

    for (int joint = 1; joint <= 2; ++joint) {
        std::vector<float> samples;
        samples.reserve(kSampleCount);
        for (int f = 0; f < kSampleCount; ++f) {
            const double t = double(f) / double(kSampleCount - 1);
            samples.push_back(float(EncodeEulerDegreesQ14(kBendDegrees * t)));
        }
        rot.samples[joint * 4 + 0] = std::move(samples); // X coordinate
    }

    SkinningState state;
    state.rotationStream = std::move(rot);

    AnimStateDescr sd;
    sd.frameTime = kFrameTime;
    sd.skinningStates.push_back(std::move(state));

    AnimAct act;
    act.name     = "bend";
    act.duration = 1.0f;
    act.stateDescrs.push_back(std::move(sd));

    AnimGroup group;
    group.name = "test";
    group.acts.push_back(std::move(act));

    anim->groups.push_back(std::move(group));
    cs.scene.animations = std::move(anim);

    return cs;
}
```

If `EncodeEulerDegreesQ14` is `static`/anonymous-namespace above
`BuildSkinnedCube`, it is already visible here — do not duplicate it. Check the
includes at the top of the file: `<Onyx/Parsers/AnimationData.h>` arrives
transitively through `SceneNode.h`, so no new include should be needed; add one
only if the build says otherwise.

- [ ] **Step 6: Run the test and watch it pass**

```bash
cmake --build build 2>&1 | tail -20
ctest --test-dir build -R OnyxAnimClip -V
```

Expected: 3 test cases, all assertions passing.

- [ ] **Step 7: Prove the corpus did not move**

```bash
ctest --test-dir build -R "OnyxOracleCorpus|VkOracleParity" -V
```

Expected: both pass. `VkOracleParity` may report `SKIP_RETURN_CODE 77` on a
machine with no suitable GPU — a skip is acceptable here, a failure is not.

- [ ] **Step 8: Commit**

```bash
git add Tools/OnyxOracle/CorpusScenes.h Tools/OnyxOracle/CorpusScenes.cpp Tests/animclip_test.cpp Tests/CMakeLists.txt
git commit -m "test(render): Synthetic animated corpus scene, proven without a device"
```

---

### Task 2: The joint palette buffer becomes writable

Pure mechanism change with zero behaviour change. Isolated into its own task
precisely so `VkOracleParity` can adjudicate it alone: if the images move here,
the memory-type change is at fault and nothing else can be blamed.

**Files:**
- Modify: `Source/Rendering/SceneRendererVk.cpp` (the `GpuBatch` struct's palette fields, the allocation around line 400-440, and `Clear()`)
- Modify: `Include/Onyx/Rendering/SceneRendererVk.h` (top-comment invariant)

**Interfaces:**
- Consumes: `Resources::CreateBuffer`, the file-local `WriteMapped()` helper, `Rendering::BuildBatchPalette`.
- Produces: `GpuBatch::jointSsboMapped` (a `void*`, null-checked at build time) and a private `bool UploadBatchPalettes(std::string& err)` that rewrites every batch's palette from `m_jointPalette`. Task 3 calls it every time the pose changes.

- [ ] **Step 1: Record the invariant this change depends on**

At the top of `Include/Onyx/Rendering/SceneRendererVk.h`, inside the existing
banner comment block, add:

```
// ═══════════════════════════════════════════════════════════════════════
// PRECONDITION -- scene submission is serialized.
//
// Every buffer this class rewrites per frame (the main and sky frame UBOs,
// the overlay VBO, the background and grid UBOs, and -- since v1.1 -- each
// batch's joint palette SSBO) is host-visible, mapped once at Build() time,
// and written directly by the CPU with no double-buffering and no barrier.
//
// That is safe only because every scene submission goes through
// Resources::OneShot, which submits and then blocks on vkWaitForFences
// before returning (VkResources.cpp) -- Viewport3D::RenderFrame, the oracle,
// and RenderToImage all take that path, so the CPU can never be writing a
// buffer the GPU is still reading. kFramesInFlight == 2 belongs to the ImGui
// swapchain, which only samples the already-resolved offscreen texture and
// never has a scene command buffer in flight.
//
// If anyone builds a real in-flight scene path, EVERY buffer named above
// needs per-frame copies or a fence before its write -- not just the palette.
// ═══════════════════════════════════════════════════════════════════════
```

- [ ] **Step 2: Add the mapped pointer to `GpuBatch`**

Find the `GpuBatch` struct in `Source/Rendering/SceneRendererVk.cpp` and add,
next to `jointSsbo`:

```cpp
    void*    jointSsboMapped = nullptr; // persistently mapped, see the header's PRECONDITION
    uint32_t paletteJointCount = 0;     // entries the SSBO was sized for
```

- [ ] **Step 3: Switch the allocation to host-visible and cache the mapping**

Replace the `gb.jointSsbo = Resources::CreateBuffer(...)` block (currently
`VMA_MEMORY_USAGE_GPU_ONLY` plus `Resources::Upload`) with the mapped-write
form the file already uses for its other per-frame buffers. Follow the exact
shape of the frame-UBO allocation nearby, including its `pMappedData`
null-check-and-fail pattern — do not invent a new error style:

```cpp
    gb.paletteJointCount = uint32_t(palette.size());
    gb.jointSsbo = Resources::CreateBuffer(ctx, sizeof(glm::mat4) * palette.size(),
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                           VMA_MEMORY_USAGE_CPU_TO_GPU, err);
    if (gb.jointSsbo.buf == VK_NULL_HANDLE) {
        err = "batch '" + part.name + "' joint SSBO: " + err;
        /* keep the existing cleanup sequence for this failure path verbatim */
    }
    {
        VmaAllocationInfo info{};
        vmaGetAllocationInfo(ctx.Allocator(), gb.jointSsbo.alloc, &info);
        gb.jointSsboMapped = info.pMappedData;
        if (!gb.jointSsboMapped) {
            err = "batch '" + part.name + "' joint SSBO is not host-mapped";
            /* same cleanup sequence */
        }
    }
    WriteMapped(gb.jointSsboMapped, palette.data(), sizeof(glm::mat4) * palette.size());
```

`VK_BUFFER_USAGE_TRANSFER_DST_BIT` is dropped because nothing stages into this
buffer any more. Match the surrounding code's exact accessor names for the
allocator and allocation handle (`ctx.Allocator()`, `gb.jointSsbo.alloc`) — read
the neighbouring frame-UBO block and copy its spelling rather than trusting the
names written here.

- [ ] **Step 4: Add the reupload helper**

As a private method on `SceneRendererVk` (declare it in the header's private
section, implement it beside `Render`):

```cpp
// Rewrites every batch's palette SSBO from m_jointPalette. Cheap: a memcpy per
// batch into already-mapped memory, no allocation, no command buffer. Batches
// that carry the one-entry identity palette (unskinned) are left alone.
void SceneRendererVk::UploadBatchPalettes() {
    for (size_t i = 0; i < m_batches.size() && i < m_gpuBatches.size(); ++i) {
        auto& gb = m_gpuBatches[i];
        if (!gb.jointSsboMapped || gb.paletteJointCount == 0) continue;
        if (!m_batches[i].hasSkeleton || m_batches[i].jointMap.empty()) continue;

        std::vector<glm::mat4> palette =
            Rendering::BuildBatchPalette(m_jointPalette, m_batches[i].jointMap);
        if (palette.empty()) continue;
        if (palette.size() > gb.paletteJointCount) palette.resize(gb.paletteJointCount);

        WriteMapped(gb.jointSsboMapped, palette.data(), sizeof(glm::mat4) * palette.size());
    }
}
```

Use the file's own names for the batch containers — read the class's private
section and match them; `m_gpuBatches` here is a placeholder for whatever the
per-batch GPU vector is actually called.

- [ ] **Step 5: Clear the pointer on teardown**

In `Clear(ctx)`, set `gb.jointSsboMapped = nullptr;` and
`gb.paletteJointCount = 0;` wherever the batch's buffers are destroyed, so a
stale mapping can never be read after the allocation is gone.

- [ ] **Step 6: Build and prove nothing moved**

```bash
cmake --build build 2>&1 | tail -20
ctest --test-dir build -R "VkOracleParity|VkSceneSmoke|RenderToImageSmoke|OracleReproducible" -V
```

Expected: all pass (or skip with 77 for lack of a GPU — never fail). This is the
gate that adjudicates this task. If `VkOracleParity` reports any tolerance flag
it did not report on `main`, stop and diagnose; do not proceed to Task 3 and do
not touch a golden.

- [ ] **Step 7: Commit**

```bash
git add Include/Onyx/Rendering/SceneRendererVk.h Source/Rendering/SceneRendererVk.cpp
git commit -m "refactor(render): Make the joint palette SSBO host-writable"
```

---

### Task 3: The animation API

**Files:**
- Modify: `Include/Onyx/Rendering/SceneRendererVk.h` (public animation block + private state)
- Modify: `Source/Rendering/SceneRendererVk.cpp` (`Build` captures rest state and clip; the four new methods)
- Modify: `Source/Rendering/AnimationPlayer.cpp` (delete two diagnostic blocks)

**Interfaces:**
- Consumes: Task 2's `UploadBatchPalettes()`; `AnimationPlayer::{SetAnimation, Stop, Update, IsPlaying, GetTime, ComputeJointMatrices}`.
- Produces: the public surface Task 4 and Task 6 both call:

```cpp
bool  HasAnimations() const;
const Parsers::AnimationData* GetAnimationData() const;
AnimationPlayer* GetAnimPlayer();
void  SetAnimation(int groupIdx, int actIdx);
void  StopAnimation();
bool  UpdateAnimation(float dt);
```

- [ ] **Step 1: Capture rest state and the clip in `Build()`**

Where `Build()` currently does
`m_jointPalette = Rendering::ComputeJointPalette(*scene.skeleton, &m_jointWorldPos);`,
keep that line and add immediately after it:

```cpp
        // The rest pose is kept immutable so StopAnimation() can restore it
        // without a rebuild. GL overwrote its only copy with std::move and
        // could not go back -- see the spec's "deliberate departures".
        m_restPalette      = m_jointPalette;
        m_restJointWorldPos = m_jointWorldPos;
```

And where `Build()` stores the scene's other shared data, add:

```cpp
    m_animData = scene.animations;   // std::shared_ptr<Parsers::AnimationData>, may be null
```

Declare in the private section: `std::vector<glm::mat4> m_restPalette;`,
`std::vector<glm::vec3> m_restJointWorldPos;`,
`std::shared_ptr<Parsers::AnimationData> m_animData;`,
`std::unique_ptr<AnimationPlayer> m_animPlayer;`,
`float m_lastAppliedAnimTime = -1.0f;`. Match `m_jointWorldPos`' element type
when declaring `m_restJointWorldPos` — read it, do not assume `vec3`.

`Clear()` must reset all five, and `m_animPlayer.reset()` — GL's `Clear()` did
exactly that.

- [ ] **Step 2: Write the failing gate**

The behavioural gate is a new oracle mode; it is written in full in Task 4.
Write and run Task 4's Step 1-2 now, before implementing the methods below, so
the first thing you see is a real failure. Return here once it fails to compile
on the missing `SetAnimation`.

- [ ] **Step 3: Implement the four methods**

Beside `Render()` in `Source/Rendering/SceneRendererVk.cpp`:

```cpp
// ── Animation ─────────────────────────────────────────────────────────────
// Names mirror the deleted GL SceneRenderer's animation block exactly, so
// consumers port across without rewriting call sites.

void SceneRendererVk::SetAnimation(int groupIdx, int actIdx) {
    if (!m_animData || !m_skeleton) return;
    if (!m_animPlayer) m_animPlayer = std::make_unique<AnimationPlayer>();
    m_animPlayer->SetAnimation(m_animData.get(), groupIdx, actIdx, m_skeleton.get());
    m_lastAppliedAnimTime = -1.0f;   // force the next UpdateAnimation to apply
}

void SceneRendererVk::StopAnimation() {
    if (m_animPlayer) m_animPlayer->Stop();

    // Restore the pose Build() captured. Unlike GL, this needs no rebuild.
    m_jointPalette  = m_restPalette;
    m_jointWorldPos = m_restJointWorldPos;
    UploadBatchPalettes();
    m_lastAppliedAnimTime = -1.0f;
}

bool SceneRendererVk::UpdateAnimation(float dt) {
    if (!m_animPlayer) return false;

    bool changed = false;
    if (m_animPlayer->IsPlaying()) {
        changed = m_animPlayer->Update(dt);
    } else {
        // A paused player can still be moved by the UI through SetTime/
        // SetFrame -- that is what scrubbing the Dopesheet does, and Update()
        // never runs for it. Without this branch playback would work while
        // scrubbing silently did nothing.
        if (m_animPlayer->GetTime() != m_lastAppliedAnimTime) changed = true;
    }
    if (!changed) return false;

    std::vector<glm::mat4> animated = m_animPlayer->ComputeJointMatrices();
    if (!animated.empty()) {
        m_jointPalette = std::move(animated);
        m_jointWorldPos.resize(m_jointPalette.size());
        for (size_t i = 0; i < m_jointPalette.size(); ++i)
            m_jointWorldPos[i] = glm::vec3(m_jointPalette[i][3]);   // skeleton overlay follows
        UploadBatchPalettes();
    }
    m_lastAppliedAnimTime = m_animPlayer->GetTime();
    return true;
}
```

Plus the three trivial accessors (`HasAnimations`, `GetAnimationData`,
`GetAnimPlayer`) inline in the header, matching GL's bodies.

- [ ] **Step 4: Delete the two leftover diagnostic blocks**

`Source/Rendering/AnimationPlayer.cpp` logs `[BindDiag]` on every `Reset()` —
which runs on every `SetAnimation` and every `Stop` — and `[BakeDiag]` plus a
four-state loop on every bake. Both are scaffolding for long-fixed bugs and
both are about to start running on a real playback loop. Delete both blocks.
Keep the `[AnimPlayer] Playing '...'` line in `SetAnimation`: that one names a
clip the user chose and fires once per choice.

- [ ] **Step 5: Build**

```bash
cmake --build build 2>&1 | tail -20
```

Expected: clean. Task 4's gate is what proves the behaviour.

- [ ] **Step 6: Commit** (after Task 4 goes green — this task and its gate land together)

---

### Task 4: The `VkAnimation` gate

**Files:**
- Modify: `Tools/OnyxOracle/Main.cpp` (a `--vk-animation-smoke` mode + its argv line + its usage text)
- Modify: `Tools/OnyxOracle/CMakeLists.txt` (`add_test(NAME VkAnimation ...)`)

**Interfaces:**
- Consumes: `BuildAnimatedChain()` (Task 1), the animation API (Task 3), `Onyx::Rendering::RenderToImage`, `Onyx::TestKit::CompareRGBA`.
- Produces: nothing downstream; this is a leaf gate.

- [ ] **Step 1: Write the gate**

In `Tools/OnyxOracle/Main.cpp`, beside `RunVkSceneSmoke()`, add
`int RunVkAnimationSmoke()`. Read `RunRenderToImageSmoke()` first and reuse its
request setup verbatim — camera, size, and error handling should look identical;
only the four renders below are new.

The four renders, all of the same scene in one process:

1. **(a) reference** — `Build()`, no `SetAnimation`, render.
2. **(b) set, not advanced** — `SetAnimation(0, 0)`, `UpdateAnimation(0.0f)`, render. Must match (a).
3. **(c) mid-clip** — `GetAnimPlayer()->SetPlaying(false)`, `GetAnimPlayer()->SetTime(0.5f)`, `UpdateAnimation(0.0f)` — which must return `true`, proving the paused/scrub branch — then render. Must differ from (a).
4. **(d) stopped** — `StopAnimation()`, render. Must match (a).

Comparisons use `Onyx::TestKit::CompareRGBA` with the same tolerance the
existing smokes use. (a)-vs-(b) and (a)-vs-(d) assert *within* tolerance;
(a)-vs-(c) asserts *outside* it by a wide margin — print the actual differing
percentage on both success and failure, the way `VkOracleParity` prints its
per-scene numbers, so a future tuning argument has data instead of opinion.

Return `77` (skip) when `VkContext::Init` fails for lack of a device, matching
every other Vk mode in this file. Return `1` on any tolerance violation or if
`ctx.ValidationMessageCount() != 0`.

Write out all four PNGs (`vk-animation-a-reference.png` etc.) next to the other
smokes' output, so a human can look at what the numbers claim.

- [ ] **Step 2: Wire the mode and the test**

In `main()`, beside the other modes:

```cpp
    if (argc >= 2 && std::strcmp(argv[1], "--vk-animation-smoke") == 0) {
        return RunVkAnimationSmoke();
    }
```

Add the mode to the usage text block near the top of the file (every other mode
documents itself there). In `Tools/OnyxOracle/CMakeLists.txt`, beside
`VkSceneSmoke`:

```cmake
add_test(NAME VkAnimation COMMAND onyx-oracle --vk-animation-smoke)
set_tests_properties(VkAnimation PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")
```

- [ ] **Step 3: Run it and watch it fail**

Run this *before* Task 3's Step 3 implementation exists:

```bash
cmake --build build 2>&1 | tail -20
```

Expected: compile error on `SetAnimation` not being a member. Then implement
Task 3 Step 3 and return.

- [ ] **Step 4: Run it and watch it pass**

```bash
ctest --test-dir build -R VkAnimation -V
```

Expected: pass, with (a)-vs-(c) reporting a large differing percentage. If
(a)-vs-(c) comes back *within* tolerance, the renderer is ignoring the
animation — that is the exact defect this gate exists to catch, so diagnose it,
do not loosen the threshold.

- [ ] **Step 5: Full suite**

```bash
cmake --build build && ctest --test-dir build
```

Expected: everything green or skipped-77. `VkOracleParity` in particular.

- [ ] **Step 6: Commit Tasks 3 and 4 together**

```bash
git add Include/Onyx/Rendering/SceneRendererVk.h Source/Rendering/SceneRendererVk.cpp Source/Rendering/AnimationPlayer.cpp Tools/OnyxOracle/Main.cpp Tools/OnyxOracle/CMakeLists.txt
git commit -m "feat(render): Play skeletal animation on the Vulkan renderer"
```

---

### Task 5: Visibility culling and the bone color knob

**Files:**
- Modify: `Source/Rendering/SceneRendererVk.cpp` (`Render()`'s batch loop; `RenderSkeleton`'s color)
- Modify: `Tools/OnyxOracle/Main.cpp` (extend `RunVkAnimationSmoke` with a fifth render)
- Modify: `Source/App/SettingsWindow.cpp` or wherever the bone picker is disabled (grep for the tooltip text)

**Interfaces:**
- Consumes: `Rendering::RenderBatch::isVisible`, `Services::AppConfig::{boneR, boneG, boneB}`.
- Produces: nothing downstream.

- [ ] **Step 1: Add the failing culling check**

In `RunVkAnimationSmoke()`, after render (d), add render (e): set
`renderer.GetBatches()[0].isVisible = false`, render, and assert the image
**differs** from (a). Build and run — it must fail, because `Render()` does not
read the flag yet.

```bash
cmake --build build && ctest --test-dir build -R VkAnimation -V
```

Expected: FAIL on the (a)-vs-(e) assertion.

- [ ] **Step 2: Read the flag**

In `Render()`'s batch loop, skip invisible batches, matching GL's
`if (!batch->gpuMesh || !batch->isVisible) continue;` — minus the `gpuMesh`
half, which does not exist on this path:

```cpp
        if (!m_batches[i].isVisible) continue;
```

Apply it in every pass the loop makes (sky, opaque, blended) — read the function
and place it consistently. Leave `isHighlighted` unread: the outline pass does
not exist, and half-wiring it would be worse than the declared gap.

- [ ] **Step 3: Run it and watch it pass**

```bash
cmake --build build && ctest --test-dir build -R VkAnimation -V
```

- [ ] **Step 4: Read the bone color from config**

In `RenderSkeleton`, replace the hardcoded bone color with
`AppConfig::Get()`'s `boneR/boneG/boneB`, falling back to the current hardcoded
values when `Get()` returns null (the oracle runs with no config — check how
`RenderGrid`'s caller handles exactly this and mirror it).

- [ ] **Step 5: Re-enable the picker**

Grep for the disabled bone picker and its tooltip:

```bash
grep -rn "boneR" Source/App/ | head
```

Remove the `BeginDisabled`/tooltip pair for the bone color only. The wireframe
and outline pickers stay disabled — their passes still do not exist.

- [ ] **Step 6: Full suite and commit**

```bash
cmake --build build && ctest --test-dir build
git add -u Source Tools
git commit -m "feat(render): Honor per-batch visibility and the bone color knob"
```

---

### Task 6: Wire the Shell back up

The UI was not rewritten away — it was removed wholesale at `71fe575`
(`feat(shell): Viewport and viewers on Vulkan textures`) when there was no
Vulkan animation API to drive it. Recover it rather than reinventing it.

**Files:**
- Modify: `Source/Viewers/Viewport3D.cpp` (transport bar, clip browser, per-frame update, active-player registration)
- Modify: `Include/Onyx/Viewers/Viewport3D.h` (re-declare `DrawTransportBar`)

**Interfaces:**
- Consumes: Task 3's animation API; `Onyx::App::SetActiveAnimationPlayer`.
- Produces: a non-null `App::GetActiveAnimationPlayer()` while a clip is loaded — which is all `Dopesheet` and `AnimCurveView` need; neither panel changes.

- [ ] **Step 1: Read what was removed**

```bash
git show 71fe575^:Source/Viewers/Viewport3D.cpp > /tmp/viewport3d-pre-vulkan.cpp
```

The pieces, at their line numbers in that file: `SetActiveAnimationPlayer(nullptr)`
on teardown (27, 44); the transport-height layout block (124-133); the
per-frame `UpdateAnimation(dt)` (142); the transport bar draw call (266-268);
`SetActiveAnimationPlayer(...GetAnimPlayer())` on scene load (486); the clip
browser calling `SetAnimation(ig, ia)` (533); and `DrawTransportBar()` itself
(694 onward).

- [ ] **Step 2: Port them onto the Vulkan member**

The old code drives `m_sceneRenderer` (the GL renderer, `unique_ptr`); the
current code drives `m_vk->sceneRendererVk` (a direct member). The method names
are identical by design, so this is a receiver swap and a null-check change, not
a rewrite. The per-frame call replaces the comment that currently reads
"Camera flight animation only (mesh animation has no Vulkan API...)" in
`Viewport3D::Draw()`:

```cpp
    if (m_vk && m_vk->targetCreated && m_vk->sceneRendererVk.UpdateAnimation(dt))
        /* request a repaint the same way the camera-flight branch below does */;
```

Read the camera-flight branch immediately after it and mirror however it
signals "redraw" — do not invent a second mechanism.

- [ ] **Step 3: Build and run the app**

```bash
cmake --build build
```

Then launch `MinimalViewer` or `OnyxBox` on a container with a skinned model
carrying clips, and confirm by eye: the transport bar appears under the
viewport, play animates the mesh, the Dopesheet shows keys, scrubbing moves the
model, and stop returns it to the rest pose.

This step has no automated gate — it is UI. Say plainly in the report what was
observed and what was not.

- [ ] **Step 4: Full suite and commit**

```bash
ctest --test-dir build
git add Include/Onyx/Viewers/Viewport3D.h Source/Viewers/Viewport3D.cpp
git commit -m "feat(shell): Restore the animation transport and clip browser"
```

---

### Task 7: Release the version

**Files:**
- Modify: `CHANGELOG.md` (a v1.1.0 section; edit the "Known gaps" text)
- Modify: `CMakeLists.txt` (project version → 1.1.0)
- Modify: `Include/Onyx/Version.h` (regenerated by configure — verify, do not hand-edit)
- Modify: `README.md` only if it states the animation gap

**Interfaces:** none.

- [ ] **Step 1: Bump the version**

Find the `project(... VERSION 1.0.0 ...)` line in the root `CMakeLists.txt` and
set `1.1.0`. Then reconfigure and confirm the checked-in header followed:

```bash
cmake --preset debug
grep -n "1\.1\.0" Include/Onyx/Version.h
```

Expected: the version header reports 1.1.0. v1.0.0's changelog records that a
stale `build/generated/Onyx/Version.h` used to shadow this — configure now
removes it, so a mismatch here means something regressed.

- [ ] **Step 2: Write the changelog section**

Add a `## v1.1.0` section above v1.0.0's. Under **Added**: the animation API
(naming the four public methods and that they mirror the GL names deliberately),
the palette buffer's serialization precondition, per-batch culling, the bone
color knob, and the `VkAnimation` gate with what it actually proves.

Then **edit v1.1.0's own "Known gaps" list** — do not leave v1.0.0's text
implying animation is still missing. What stays open, verbatim in kind: no
outline/hover-highlight pass, no wireframe or matcap, the wireframe and outline
color pickers still dead knobs, `install()`/`export()` (G4), `PathUtils` at
global scope (G5's open half), and the parity gate's detection floor.

State plainly that `RenderBatch::isHighlighted` is still unread.

- [ ] **Step 3: Full suite, clean tree**

```bash
cmake --build build && ctest --test-dir build
git status --short
```

- [ ] **Step 4: Commit**

```bash
git add CHANGELOG.md CMakeLists.txt Include/Onyx/Version.h README.md
git commit -m "chore(release): v1.1.0 — Vulkan skeletal animation"
```

---

## Self-review notes

- **Spec coverage.** Palette mechanism → Task 2. Public API → Task 3. Both
  deliberate departures from GL → Task 3 Steps 1 and 3 (rest pose) and Step 4
  (`[PalDiag]` never returns; its two live siblings deleted). Paused/scrub
  branch → Task 3 Step 3 plus its own gate in Task 4 render (c). Culling and
  bone color → Task 5. Shell wiring → Task 6. Gates 1-5 → Tasks 2, 4, 5, and
  the full-suite step in every task. Out-of-scope items → restated in Task 7
  Step 2 so they stay declared.
- **The spec's gate 2 was corrected during planning** and the spec now matches:
  it originally compared against a "rest-pose golden" that does not exist,
  because the corpus carries no animation data and no golden may be added
  without moving the parity anchor. The gate is now self-contained (four
  renders of one scene, compared against each other in one process).
- **Task ordering is load-bearing.** Task 2 lands alone so `VkOracleParity`
  adjudicates the memory-type change with nothing else in the diff. Tasks 3 and
  4 commit together because neither is testable without the other.
- **Names to verify against the source, not this document:** the per-batch GPU
  vector (`m_gpuBatches` here), `m_jointWorldPos`' element type, the VMA
  allocator/allocation accessors, and how `Viewport3D` signals a redraw. Each is
  flagged at its step.
