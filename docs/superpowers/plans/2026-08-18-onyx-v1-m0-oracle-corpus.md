# Onyx v1 M0 — Oracle Corpus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the GL oracle — a headless render tool plus a deterministic
synthetic corpus (PNG + JSON references) that M4's Vulkan renderer must
reproduce — and land the mount/ByteRange Core work carried from M3.

**Architecture:** A new `Tools/OnyxOracle` executable owns the headless GL
context (ported from GoWToolkit's proven `HeadlessGL`) and drives the SAME
`SceneRenderer` the viewport drives — nothing that decides how a pixel
looks lives in the tool. Corpus scenes are built programmatically as
`Parsers::SceneData` (no game data, no files, no randomness), rendered to
PNG + a per-batch JSON report with canonical formatting so byte-comparison
is the equality test. In parallel, Core gains what the spec already names:
`ByteRange` source identity on entries (§5.4), mount processing at open
(§5.2), and the OnyxBox example module grows a mounted `.obxpak` archive
format to exercise all of it end-to-end through the generic CLI.

**Tech Stack:** C++20 MSVC, OpenGL 3.3 core (existing `Onyx_Render`), GLFW
hidden window (tool-side only), stb_image_write (new vendored header),
FNV-1a for pixel/palette hashes, ctest `SKIP_RETURN_CODE` for GL tests.

**Spec:** `docs/design/2026-08-18-onyx-v1-architecture.md` (§5.2 mounts,
§5.4 ByteRange, §8 render floors). Roadmap: `2026-08-18-onyx-v1-roadmap.md`
§M0 (deliverables + gate) — including the carried-from-M3 bullet.

## Global Constraints

- The build dir `build/` is a working MSVC/ninja configure. NEVER reconfigure
  or delete it; a configure/build failure is STOP-and-report.
- No game or format name in `Onyx::Core`/`Onyx::Render` sources (spec §13).
  OnyxBox names stay in `Examples/OnyxBox`; GLFW/imgui stay out of Core and
  Render (LayerGuard enforces; `Tools/` is a composition root like Shell and
  MAY use GLFW).
- Every new public header must compile standalone (self-sufficient includes).
- Determinism is a requirement, not a nicety: no timestamps, no randomness,
  no pointer values, no `std::unordered_*` iteration order in anything the
  oracle writes. Floats print via the canonical formatter of Task 4 only.
- Salvage policy: new Error-severity diags in existing paths fail CI unless
  allowlisted (standing rule since M2).
- Tests: doctest via existing harness; new suites registered in
  `Tests/CMakeLists.txt` following the existing filter pattern (ctest name
  must match a real TEST_CASE filter — no silently-empty entries).
- Commits: Conventional Commits, Sentence-case subject, no attribution
  trailers, stage explicit paths only.
- GL-requiring ctests must skip cleanly (exit 77 + `SKIP_RETURN_CODE 77`)
  when a context cannot be created, so headless CI stays green until M4's
  lavapipe job.

## File Structure

```
third_party/stb/stb_image_write.h          (new, vendored, Task 1)
Tools/OnyxOracle/CMakeLists.txt            (new, Task 1)
Tools/OnyxOracle/HeadlessGL.h/.cpp         (new, Task 1 — port from GoWToolkit)
Tools/OnyxOracle/CorpusTextures.h/.cpp     (new, Task 2 — pure, no GL)
Tools/OnyxOracle/CorpusScenes.h/.cpp       (new, Task 3 — pure, no GL)
Tools/OnyxOracle/RenderReport.h/.cpp       (new, Task 4 — pure, no GL)
Tools/OnyxOracle/Main.cpp                  (new, Task 1 stub; Task 5 full)
Tests/Golden/corpus/*.png|*.json           (new, Task 5 — committed refs)
Include/Onyx/Domain/ByteRange.h            (new, Task 6)
Include/Onyx/Domain/Entry.h                (modify, Task 6)
Source/Cli/Commands.cpp                    (modify, Tasks 6+7)
Include/Onyx/Modules/GameModule.h          (modify, Task 7 — ContainerContext)
Include/Onyx/Modules/Workspace.h + Source  (modify, Task 7 — mount at open)
Examples/OnyxBox/OnyxBoxModule.h/.cpp      (modify, Tasks 6+8)
Tests/oracle_corpus_test.cpp               (new, Tasks 2-4)
Tests/mounts_test.cpp                      (new, Tasks 7-8)
```

Tasks 1-5 (oracle) and 6-8 (Core carry-over) are two independent tracks;
within each track the order is strict.

---

### Task 1: Vendored stb + OnyxOracle target + HeadlessGL port

**Files:**
- Create: `third_party/stb/stb_image_write.h` — copy verbatim from
  `E:/CodingProjects/GoWToolkit/third_party/stb/stb_image_write.h`
- Create: `Tools/OnyxOracle/CMakeLists.txt`, `Tools/OnyxOracle/HeadlessGL.h`,
  `Tools/OnyxOracle/HeadlessGL.cpp`, `Tools/OnyxOracle/Main.cpp` (stub)
- Modify: root `CMakeLists.txt` (add_subdirectory; extend the source-list
  completeness check's exclusions to cover `Tools/` the same way it excludes
  `Examples/` — read the check before editing)
- Test: ctest `OracleGlSmoke` (the tool's own `--gl-smoke` mode)

**Interfaces:**
- Consumes: `Onyx::Render` (SceneRenderer, ShaderManager), glfw, glad.
- Produces: `class Onyx::OracleTool::HeadlessGL` —
  `bool Init(std::string&)`, `bool BeginFrame(int,int,std::string&)`,
  `bool EndFrame(std::vector<uint8_t>&,std::string&)`,
  `static bool WritePng(const std::filesystem::path&,int,int,
  const std::vector<uint8_t>&,std::string&)`. Task 5 uses all of these.

- [ ] **Step 1: Vendor the header + write the CMake target.**

```cmake
add_executable(onyx-oracle
    Main.cpp
    HeadlessGL.cpp
    # Tasks 2-4 append CorpusTextures.cpp, CorpusScenes.cpp, RenderReport.cpp
)
target_include_directories(onyx-oracle PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
target_link_libraries(onyx-oracle PRIVATE Onyx::Render glfw)
onyx_apply_common(onyx-oracle)
add_test(NAME OracleGlSmoke COMMAND onyx-oracle --gl-smoke)
set_tests_properties(OracleGlSmoke PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")
```

  Root CMakeLists: `add_subdirectory(Tools/OnyxOracle)` after Examples.

- [ ] **Step 2: Port HeadlessGL.**
  Source of truth: `E:/CodingProjects/GoWToolkit/Source/cli/HeadlessGL.{h,cpp}`.
  Port verbatim except: namespace becomes `Onyx::OracleTool`; the
  ShaderManager init must match THIS repo's `ShaderManager::Initialize()`
  signature and shader-path resolution (read
  `Source/Rendering/ShaderManager.cpp` first — resolve shader paths relative
  to the executable the same way the Shell does; if that helper is
  Shell-only, copy the few resolution lines into the tool, do not link
  Shell). Keep the MSAA(4x)+resolve FBO pair and the top-down RGBA readback
  exactly as the source — the GoWToolkit header comment explains why the
  duplication is confined to FBO setup.

- [ ] **Step 3: Stub Main with the smoke mode.**

```cpp
// Main.cpp (Task 1 form)
#include "HeadlessGL.h"
#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--gl-smoke") == 0) {
        Onyx::OracleTool::HeadlessGL gl;
        std::string err;
        if (!gl.Init(err)) { std::fprintf(stderr, "skip: %s\n", err.c_str()); return 77; }
        if (!gl.BeginFrame(64, 64, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        std::vector<uint8_t> rgba;
        if (!gl.EndFrame(rgba, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        return rgba.size() == 64u * 64u * 4u ? 0 : 1;
    }
    std::fprintf(stderr, "onyx-oracle: no command\n");
    return 1;
}
```

- [ ] **Step 4: Build and run.** `ninja -C build onyx-oracle`, then
  `ctest --test-dir build -R OracleGlSmoke --output-on-failure`. On this
  machine (desktop session) expect PASS; the 77 path is for CI.

- [ ] **Step 5: Commit.**
  `build(oracle): Add the headless GL tool skeleton and vendored stb writer`
  (stage `third_party/stb/stb_image_write.h Tools/OnyxOracle CMakeLists.txt`).

### Task 2: Procedural corpus textures (pure)

**Files:**
- Create: `Tools/OnyxOracle/CorpusTextures.h`, `CorpusTextures.cpp`
- Test: `Tests/oracle_corpus_test.cpp` (new file; register ctest
  `OnyxOracleCorpus` with a filter matching its TEST_CASE names, pattern-
  matching the existing `Tests/CMakeLists.txt` entries). The test target
  compiles the corpus .cpp files directly (they are GL-free); do NOT link
  onyx-oracle.

**Interfaces:**
- Produces (all return `std::unique_ptr<Parsers::TextureData>` with
  `glInternalFormat` left at default RGBA8, `isCompressed=false`):

```cpp
namespace Onyx::OracleTool {
// 8-px checker of colorA/colorB (RGBA bytes), size x size.
std::unique_ptr<Parsers::TextureData> MakeChecker(
    uint32_t size, std::array<uint8_t,4> a, std::array<uint8_t,4> b,
    std::string name);
// Vertical gradient top -> bottom.
std::unique_ptr<Parsers::TextureData> MakeGradient(
    uint32_t size, std::array<uint8_t,4> top, std::array<uint8_t,4> bottom,
    std::string name);
// Solid fill.
std::unique_ptr<Parsers::TextureData> MakeSolid(
    uint32_t size, std::array<uint8_t,4> c, std::string name);
// Tangent-space normal map: flat +Z everywhere except an 8-px bump grid
// (deterministic dome normals) so the Normal role visibly shades.
std::unique_ptr<Parsers::TextureData> MakeBumpNormal(
    uint32_t size, std::string name);
}
```

- [ ] **Step 1: Write the failing tests** in `Tests/oracle_corpus_test.cpp`:
  `MakeChecker(16,...)` → width/height 16, `pixels.size()==16*16*4`,
  pixel(0,0)==a, pixel(8,0)==b; `MakeGradient` → row 0 == top, last row ==
  bottom; `MakeBumpNormal` → every texel decodes to a unit-length normal
  with z>0 (|len−1| < 0.02 after v/255*2−1); two calls with the same args
  produce byte-identical `pixels` (determinism).
- [ ] **Step 2: Run, expect FAIL** (undefined symbols).
- [ ] **Step 3: Implement.** Plain nested loops; integer math for checker;
  float lerp for gradient (round via `uint8_t(a + (b-a)*t + 0.5f)`); bump
  domes: per 8x8 cell, height h = max(0, 1−4r²) from the cell center,
  normal = normalize(−dh/dx, −dh/dy, 1), encoded ×127.5+127.5.
- [ ] **Step 4: Run tests → PASS.** Full suite still green.
- [ ] **Step 5: Commit.** `feat(oracle): Generate deterministic corpus textures`

### Task 3: Corpus scene builders (pure)

**Files:**
- Create: `Tools/OnyxOracle/CorpusScenes.h`, `CorpusScenes.cpp`
- Test: extend `Tests/oracle_corpus_test.cpp`

**Interfaces:**
- Consumes: Task 2 makers; `Parsers::SceneData/MeshPart/MaterialDesc/
  TextureRole/BlendMode`; `Domain::GpuVertex`; `Parsers::ObjectData/Joint`.
- Produces:

```cpp
namespace Onyx::OracleTool {
struct CorpusScene {
    std::string           name;      // file stem: "sphere-grid" etc.
    Parsers::SceneData    scene;
    glm::mat4             view;      // fixed camera, defined per scene
    glm::mat4             proj;      // perspective, fixed fov/aspect/near/far
    int                   width  = 512;
    int                   height = 512;
};
// The whole corpus in canonical order. Deterministic: same output every call.
std::vector<CorpusScene> BuildCorpus();
// Individual builders (BuildCorpus composes these four, in this order):
CorpusScene BuildSphereGrid();   // 3x3 UV-spheres, all 9 TextureRoles bound
CorpusScene BuildSkinnedCube();  // 3-joint chain, rest pose != bind pose
CorpusScene BuildBlendStack();   // checker floor + Normal/Additive/Subtractive quads
CorpusScene BuildJointChain200();// 200-joint spiral of skinned segments
}
```

**Scene definitions (exact, so two implementers build the same thing):**

- `sphere-grid`: 9 UV-spheres (24 stacks × 32 slices, radius 0.8) at
  x,y ∈ {−2,0,2}, z=0. One material per sphere; every material binds ALL
  nine `TextureRole`s from a 10-entry flat texture pool (textures live once,
  materials index them — the pool has 10 entries, not 81):
  Diffuse=MakeChecker(64, red/white), Normal=MakeBumpNormal(64),
  Occlusion=MakeGradient(64, white→mid-grey), Gloss=MakeGradient(64,
  black→white), Height=MakeSolid(64, mid-grey), Scatter=MakeSolid(64,
  {200,60,60,255}), Detail=MakeChecker(32, blue/white), Emissive=
  MakeSolid(64, black) for rows 0-1 and MakeSolid(64, {0,80,0,255}) for
  row 2, EnvMap=MakeGradient(64, sky-blue→white). Sphere (col,row):
  `baseColor = {0.4+0.3*col, 0.4+0.3*row, 0.6, 1}`, metallic ∈ {0, 0.5, 1}
  by column. Camera: eye (0,−7,2.5) → target (0,0,0), up +Z; proj 45°,
  1:1 aspect, near 0.1, far 100.
- `skinned-cube`: one 1×1×4 box along +Z with 8 vertex rings, skinned to a
  3-joint chain at z ∈ {0, 1.33, 2.67}: each vertex weights to the two
  nearest joints by distance (normalized; boneWeights.xy, boneIndices.xy;
  remaining lanes zero). Bind pose = straight chain (bindToJointMat =
  inverse of the straight-chain world transform). Rest pose bends joints 1
  and 2 by +30° around X each. Diffuse = MakeChecker(64, orange/white); all
  other roles absent. IMPORTANT — before writing this builder, READ
  `SceneRenderer::ComputeJointPalette` in `Source/Rendering/
  SceneRenderer.cpp` and populate exactly the ObjectData fields the
  no-animation path consumes; the acceptance check in Task 5 asserts the
  cube renders visibly bent. Camera: eye (4,−6,2) → (0,0,1.5), up +Z.
- `blend-stack`: floor quad 6×6 at z=0 (MakeChecker(64, grey/white),
  opaque), then three 2×2 quads at z ∈ {0.5, 1.0, 1.5}, overlapping in
  view: blendMode Normal (white texture, baseColor alpha 0.5), Additive
  (MakeSolid {40,40,255,255}), Subtractive (MakeSolid {60,60,60,255}).
  Camera: eye (0,−6,4) → (0,0,0.8), up +Z.
- `joint-chain-200`: 200 segments, each a 0.1×0.1×0.3 box skinned with
  weight 1.0 to its own joint (single weight lane). Joint k's rest
  transform advances 0.32 along the parent's +Z and yaws 7° — a
  deterministic spiral. Bind = straight chain. Diffuse MakeSolid
  {200,200,60,255}. This exceeds any fixed 64/128 palette uniform limit on
  purpose — if the batch splits or the shader caps, the JSON report records
  it and THAT is the oracle datum. Camera: eye (12,−12,8) → (0,0,4), up +Z.

- [ ] **Step 1: Failing tests** (shape-level, no GL): corpus has 4 scenes in
  canonical order with the exact names above; sphere-grid: 9 parts, 9
  materials, texture pool size 10, every material's `textures` map has all
  9 roles with in-range indices; skinned-cube: `HasSkeleton()`, every
  vertex's weights sum to 1±1e-4; blend-stack: exactly one material each
  with Additive and Subtractive; joint-chain-200: 200 joints, 200 parts;
  calling `BuildCorpus()` twice yields byte-identical vertex vectors
  (memcmp on `parts[0].vertices`).
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** (UV-sphere generator ~30 lines; a box generator
  shared by the two skinned scenes).
- [ ] **Step 4: Run tests → PASS**, full suite green.
- [ ] **Step 5: Commit.** `feat(oracle): Build the four synthetic corpus scenes`

### Task 4: Canonical render report (pure)

**Files:**
- Create: `Tools/OnyxOracle/RenderReport.h`, `RenderReport.cpp`
- Test: extend `Tests/oracle_corpus_test.cpp`

**Interfaces:**
- Consumes: `Rendering::RenderBatch` (read-only), pixel buffers.
- Produces:

```cpp
namespace Onyx::OracleTool {
uint64_t Fnv1a(const void* data, size_t len);   // 64-bit offset/prime
std::string FormatFloat(float v);               // "%.6f"; "-0.000000" -> "0.000000"
// One JSON document for a rendered scene: scene name, w/h, pixelHash,
// then per-batch (in GetBatches() order): name, vertexCount, triangleCount,
// blendMode (enum name), hasTexture/hasEnvmap/hasSkeleton, metallic,
// materialColor[4], roleTexturesBound (count of nonzero GL role slots),
// paletteJointCount. Two-space indent, keys in the order listed here,
// LF line endings, no trailing whitespace: byte-stable by construction.
std::string BuildReport(const std::string& sceneName, int w, int h,
                        uint64_t pixelHash,
                        const std::vector<Rendering::RenderBatch>& batches,
                        const std::vector<size_t>& paletteJointCounts);
}
```

- [ ] **Step 1: Failing tests:** `Fnv1a("",0) == 14695981039346656037ull`;
  `Fnv1a("a",1) == 0xaf63dc4c8601ec8cull`; `FormatFloat(1.0f)=="1.000000"`;
  `FormatFloat(-0.0f)=="0.000000"`; `BuildReport` with two hand-built
  `RenderBatch` values equals a verbatim expected string (write the
  expectation exactly — this test IS the format spec); calling twice →
  identical strings.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.** `snprintf("%.6f")` + the −0 normalization;
  manual string building (no ostream locale risk). No timestamps, no paths,
  no pointers in the output — determinism constraint.
- [ ] **Step 4: Run → PASS.**
- [ ] **Step 5: Commit.** `feat(oracle): Emit canonical per-batch render reports`

### Task 5: Oracle main — render-corpus, verify, references committed

**Files:**
- Modify: `Tools/OnyxOracle/Main.cpp`, `Tools/OnyxOracle/CMakeLists.txt`
- Create: `Tests/Golden/corpus/` (4 PNG + 4 JSON, committed),
  `Tools/OnyxOracle/ReproTest.cmake`
- Test: ctest entries `OracleReproducible`, `OracleMatchesGolden`

**Interfaces:**
- Consumes: everything from Tasks 1-4 + `SceneRenderer::Build/Render` +
  `RenderBackground`.
- Produces CLI: `onyx-oracle render-corpus --out DIR` (renders all 4 scenes
  → `DIR/<name>.png` + `DIR/<name>.json`, one printed line per scene, exit
  0; exit 77 if GL init fails); `onyx-oracle verify DIR_A DIR_B`
  (byte-compares the 8 files, per-file verdict lines, exit 0 identical /
  1 differing / 2 missing file / 77 DIR_B absent entirely — document the 77
  in --help).

- [ ] **Step 1: Implement render-corpus.** Per scene:
  `HeadlessGL::BeginFrame(scene.width, scene.height)` →
  `SceneRenderer::RenderBackground({0.10f,0.11f,0.13f},{0.03f,0.03f,0.04f})`
  → `renderer.Build(scene.scene)` → `renderer.Render(view, proj,
  ShadingMode::<the Viewport3D default — read Viewport3D.cpp and use the
  same>, w, h)` → `EndFrame` → `Fnv1a(rgba)` → `WritePng` + `BuildReport`
  (palette counts: `jointMap.size()` per batch, which is the palette size
  by construction). One `SceneRenderer` per scene, `Clear()` between.
- [ ] **Step 2: Implement verify** (read both files fully, memcmp).
- [ ] **Step 3: Register the gate tests:**

```cmake
add_test(NAME OracleReproducible
         COMMAND ${CMAKE_COMMAND}
           -DORACLE=$<TARGET_FILE:onyx-oracle>
           -DWORK=${CMAKE_CURRENT_BINARY_DIR}/oracle-repro
           -P ${CMAKE_CURRENT_SOURCE_DIR}/ReproTest.cmake)
set_tests_properties(OracleReproducible PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")
add_test(NAME OracleMatchesGolden
         COMMAND onyx-oracle verify ${CMAKE_SOURCE_DIR}/Tests/Golden/corpus
                 ${CMAKE_CURRENT_BINARY_DIR}/oracle-repro/a)
set_tests_properties(OracleMatchesGolden PROPERTIES DEPENDS OracleReproducible
         SKIP_RETURN_CODE 77 LABELS "render")
```

  `ReproTest.cmake`: run `render-corpus --out ${WORK}/a`, propagate exit 77
  as 77; run again `--out ${WORK}/b`; run `verify ${WORK}/a ${WORK}/b`; any
  other nonzero → `FATAL_ERROR`.
- [ ] **Step 4: Generate and eyeball the references.** Run
  `onyx-oracle render-corpus --out Tests/Golden/corpus`, open the 4 PNGs,
  and CHECK: spheres visibly shaded with bump detail; cube visibly BENT
  (if straight, Task 3's pose fields are wrong — STOP and fix there); blend
  stack shows three distinct blending behaviors; the 200-chain spiral is
  visible. Describe all four images in the task report.
- [ ] **Step 5: Full suite + repeats.**
  `ctest --test-dir build --output-on-failure` (serial — the suite has a
  known temp-path collision under -j, ledgered for M5), then
  `ctest --test-dir build -R Oracle --repeat until-fail:3`.
- [ ] **Step 6: Commit** (two commits):
  `feat(oracle): Render and verify the corpus deterministically`
  (code + tests), then
  `test(oracle): Commit the GL reference corpus` (Tests/Golden/corpus).

### Task 6: ByteRange + 64-bit entry addressing (spec §5.4)

**Files:**
- Create: `Include/Onyx/Domain/ByteRange.h`
- Modify: `Include/Onyx/Domain/Entry.h`, `Source/Cli/Commands.cpp`,
  `Examples/OnyxBox/OnyxBoxModule.cpp` (assignments), plus every use site
  the compiler surfaces (grep first; the compiler is the authoritative list)
- Test: extend `Tests/cli_test.cpp`

**Interfaces:**

```cpp
// Include/Onyx/Domain/ByteRange.h
#pragma once
#include <cstdint>
namespace Onyx::Domain {
/// Where an entry's payload physically lives. fileIndex indexes the owning
/// Document's file table: 0 = the container file itself, 1+ = files opened
/// through the document's mount (Task 7). Empty() ranges carry no payload.
struct ByteRange {
    uint32_t fileIndex = 0;
    uint64_t offset    = 0;
    uint64_t size      = 0;
    bool Empty() const { return size == 0; }
};
}
```

- `AssetEntry` drops `uint32_t size/offset` for `ByteRange source;`. NO
  compat accessors — this is a break-and-port repo; fix every use site the
  compiler surfaces (expected: Commands.cpp list/extract/decode, OnyxBox
  parser, DocumentBrowser/InfoTab size displays).

- [ ] **Step 1: Write the failing test:** in cli_test.cpp, build a
  synthetic OBX whose TOC declares an entry size of 0xFFFFFFFF (uint32 max)
  — the parse must produce `source.size == 0xFFFFFFFFull` intact (no
  truncation anywhere in the chain) and the entry Failed-flagged by the
  existing payload-bounds check (the file is tiny). Also
  `static_assert(sizeof(Onyx::Domain::ByteRange) == 24);`. Do NOT change
  the OBX on-disk format — the TOC stays uint32; the widening under test is
  the in-memory chain.
- [ ] **Step 2: Run, expect FAIL** (field doesn't exist).
- [ ] **Step 3: Implement:** add ByteRange.h; swap the AssetEntry fields;
  chase compiler errors until clean.
- [ ] **Step 4: Full suite green** (the existing CLI list/extract/decode
  tests are the regression net).
- [ ] **Step 5: Commit.**
  `refactor(core)!: Give entries ByteRange source identity`
  (body: spec §5.4; break-and-port, no compat shims).

### Task 7: Mount processing at open + mount-aware extract (spec §5.2)

**Files:**
- Modify: `Include/Onyx/Modules/GameModule.h` (ContainerContext),
  `Include/Onyx/Modules/Workspace.h`, `Source/Modules/Workspace.cpp`,
  `Source/Cli/Commands.cpp` (extract resolves fileIndex)
- Test: `Tests/mounts_test.cpp` (new suite + ctest entry `OnyxMounts`)

**Interfaces:**
- `ContainerContext` gains two members, BOTH null/absent for plain files:
  `Vfs::IVirtualFileSystem* mountedVfs = nullptr;` and
  `std::vector<std::shared_ptr<Vfs::IFile>>* fileTable = nullptr;`.
  A module parsing through a mount opens inner files via `mountedVfs`,
  pushes each into `*fileTable`, and stamps entries' `source.fileIndex`
  with that table index. Index 0 is pre-seeded by the Workspace with the
  root container file. `Document` stores the table and the mounted VFS
  (keeps inner files alive as long as the document lives).
- `Workspace::OpenAsync` flow change: after probe picks the winner, check
  `module->Mounts()` for a spec whose `extensions` contains the opened
  path's lowercase extension (no dot); if found, call `mount(path)`; a null
  return falls through to plain-file parse WITH a Warning diag ("mount
  refused, parsing as flat file"); success stores the VFS on the Document
  and passes it in the ContainerContext.
- Extract (Commands.cpp): resolve `e.source.fileIndex` through the
  document's file table; out-of-range index = an error line for that entry,
  then continue (salvage, never abort the extract).

- [ ] **Step 1: Failing tests** in mounts_test.cpp using a stub module + an
  in-memory VFS (pattern-match the stubs in workspace_test.cpp): (a) a
  module with a MountSpec for extension "pak" receives a non-null
  `mountedVfs` when opening "x.pak" and null when opening "x.obx"; (b) the
  factory returning nullptr → parse proceeds, exactly one Warning diag;
  (c) an entry with fileIndex 1 extracts bytes from fileTable[1], not the
  root file; (d) fileIndex 99 → skip line, remaining entries still
  extracted.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** (mount-at-open in Workspace ~25 lines; extract
  signature change: pass the table, not a single IFile&).
- [ ] **Step 4: Full suite green.**
- [ ] **Step 5: Commit.**
  `feat(core): Mount archives at open and address entries through the file table`

### Task 8: OnyxBox .obxpak — the mounted example, end to end

**Files:**
- Modify: `Examples/OnyxBox/OnyxBoxModule.h/.cpp` (MountSpec + pak parse),
  the OBX test-fixture builder helpers, `Tests/mounts_test.cpp` (end-to-end)
- Test: end-to-end through `Cli::Commands`

**OBXPAK format (fixture-grade, deliberately trivial):** magic `OBP1`,
uint32 count, then per file: uint32 nameLen, name bytes, uint32 size, raw
bytes of a complete OBX container. The module's MountSpec (`label "OnyxBox
pak"`, `extensions {"obxpak"}`) mounts it as an in-memory VFS whose
`ListDirectory` returns the inner names and `OpenFile` serves each inner
OBX as an IFile. Probe: extend OnyxBox's Probe to also score `OBP1` magic
(same confidence tier as OBX1). ParseContainer with `mountedVfs`: for each
inner file, open it, push into the fileTable, parse with the existing OBX
entry parser into a child subtree named after the inner file; entries stamp
that fileIndex. Hardening mirrors OBX: count clamp, nameLen clamp (1..255),
declared size vs remaining bytes → Failed subtree + diag, never abort.

- [ ] **Step 1: Failing end-to-end test:** build an obxpak fixture with two
  inner OBX (one clean, one containing a corrupt entry); through
  `Cli::Commands`: probe picks onyxbox; `list --json` shows two subtrees;
  `extract` writes the inner entries' payloads byte-identical to what went
  into the fixture — give the two inner containers entries with IDENTICAL
  names but different bytes, so a root-file misread cannot pass; the
  corrupt entry is skipped with its error line; `--strict` exits
  kStrictErrors.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** (in-memory VFS ~50 lines in OnyxBoxModule.cpp).
- [ ] **Step 4: Full suite + LayerGuard green.**
- [ ] **Step 5: Commit.**
  `feat(examples): Teach OnyxBox a mounted pak so mounts have a consumer`

---

## Milestone Gate (from the roadmap, restated)

1. `ctest` fully green (serial) on the MSVC configure, including
   OracleReproducible and OracleMatchesGolden run 3×
   (`--repeat until-fail:3`).
2. `Tests/Golden/corpus` committed; `onyx-oracle verify` against a fresh
   render passes byte-identical on this machine.
3. Mount/ByteRange end-to-end proven by the obxpak CLI test.
4. Docs: CHANGELOG entry + roadmap M0 marked done at merge.

## Self-review notes

- Type names cross-checked against the tree at plan time:
  `Parsers::TextureData{name,width,height,pixels}`, `Domain::GpuVertex{
  position,normal,uv,color,uv1,boneWeights,boneIndices,tangent}`,
  `MeshPart{name,vertices,indices,materialId,jointMap,useBindToJoint,
  isRigid}`, `MaterialDesc{baseColor,blendMode,textures}`, `RenderBatch`
  fields as read in SceneRenderer.h, `IFile::{Read,Seek,Size,ReadAll}`.
- The two deliberately open points are flagged INSIDE the tasks that own
  them: which ObjectData fields the no-animation palette path consumes
  (Task 3: read `ComputeJointPalette` first) and the default ShadingMode
  (Task 5: read Viewport3D). Both are read-then-match, not guesses.
- The CLI `render` command deferred from M3 is satisfied by the oracle
  tool's `render-corpus` (roadmap note at the M3→M0 handoff); a
  per-container `render <path> <node>` needs a Scene decoder to exist and
  lands with M4's pixel-compare harness.
