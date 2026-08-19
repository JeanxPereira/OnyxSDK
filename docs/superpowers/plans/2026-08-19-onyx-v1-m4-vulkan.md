# Onyx v1 M4 — Vulkan Renderer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite `Onyx::Render` on Vulkan 1.3 (dynamic rendering + VMA),
prove it against the M0 GL oracle corpus within tolerance, swap the Shell
onto it, delete GL from the tree, and stand up CI with a lavapipe render
job.

**Architecture:** The new renderer grows in parallel as `Onyx_RenderVk`
(namespace `Onyx::RenderVk`) while GL keeps the app alive; the oracle tool
gains `--renderer vk` plus a tolerance `compare` command, and the parity
ctest gates every step against the committed GL goldens. Only after parity
passes does the Shell swap (GLFW `NO_API` + swapchain + `imgui_impl_vulkan`,
viewers on a Vulkan texture-upload util), and the final tasks delete GL,
glad, and the `AppConfigStub` workaround (the new renderer takes settings
as parameters — the `Onyx_Render`-not-link-complete defect dies here), then
author CI. Two floors per spec §8: `SceneRenderer` (ready) and
`RenderContext` (raw handles + `AddPass`). No backend abstraction layer.

**Tech Stack:** Vulkan 1.3 core (dynamic rendering, no render passes),
volk (loader), Vulkan-Headers, VulkanMemoryAllocator, glslang (build-time
GLSL→SPIR-V), GLFW surface, imgui_impl_vulkan — all via the repo's
established FetchContent pattern, pinned tags, no system Vulkan SDK
required to build.

**Spec:** `docs/design/2026-08-18-onyx-v1-architecture.md` §8 (two floors,
no abstraction layer, material roles), §7.1 (exception containment), §13
(no game names in Core/Render). Roadmap: `2026-08-18-onyx-v1-roadmap.md`
§M4 (deliverables, gate, carried list).

## Global Constraints

- Build dir `build/` is a working MSVC/ninja configure. NEVER reconfigure
  or delete it. New FetchContent deps make `cmake build` re-run configure
  on next ninja — that is fine and expected; DELETING the dir is not. If
  configure fails, STOP and report BLOCKED.
- vcvars64 is the BuildTools edition; from Git Bash use
  `MSYS2_ARG_CONV_EXCL="*"` when invoking cmd /c. ctest SERIAL only.
- All new Vulkan-touching ctests skip cleanly (exit 77 +
  `SKIP_RETURN_CODE 77`, label `render`) when instance/device creation
  fails — this machine has a desktop driver so locally they RUN.
- The GL goldens in `Tests/Golden/corpus` are FROZEN this milestone: no
  task may regenerate or edit them (they are the oracle). `onyx-oracle
  verify` (byte mode) stays intact for them until T11.
- No game/format names in Core or Render sources (spec §13). LayerGuard
  manifests must be updated when files move/appear; note LayerGuard greps
  includes only — link-level discipline is reviewed by humans.
- Every new public header compiles standalone.
- No behavior change may ride into GL code paths before T9 — the GL
  renderer output must stay byte-identical (the parity target must not
  move while we aim at it).
- Conventional Commits, Sentence-case subject <= 72 chars, NO attribution
  trailers, explicit-path staging.
- Vulkan idioms fixed for the whole milestone: Vulkan 1.3 core only, no
  extensions beyond what swapchain requires; dynamic rendering
  (`vkCmdBeginRendering`) everywhere, zero `VkRenderPass` objects;
  volk loads everything (no static vulkan-1 link); VMA owns every
  allocation; validation layers enabled in Debug when present, silent
  when absent; right-handed GLM with `GLM_FORCE_DEPTH_ZERO_TO_ONE` and a
  Y-flip handled in the projection (NOT negative viewport), documented in
  one place (T3 camera note).

## File Structure

```
Source/RenderVk/…, Include/Onyx/RenderVk/…   (new target Onyx_RenderVk; T11
                                              renames the namespace/target to
                                              Onyx::Render once GL is gone)
  VkContext.h/.cpp        (T1: instance/device/queues/volk/VMA boot)
  VkResources.h/.cpp      (T2: Buffer, Image2D, staging upload, one-shots)
  Shaders/*.vert|*.frag   (T3: GLSL 450 sources)
  ShaderCompile.cmake     (T3: glslang build rule → embedded .h)
  Pipelines.h/.cpp        (T3: layouts, pipelines, descriptor plumbing)
  OffscreenTarget.h/.cpp  (T4: color+depth+MSAA resolve+readback)
  SceneRendererVk.h/.cpp  (T5-T6: the ready floor)
  RenderContext.h/.cpp    (T8: the raw floor)
  TexturePool.h/.cpp      (T10: viewer-facing texture upload + ImGui binding)
Tools/OnyxOracle/         (T7: --renderer vk, compare command, parity ctest)
Source/App/Window.cpp + Platform/  (T9: NO_API, surface, swapchain, imgui_impl_vulkan)
Source/Viewers/Viewport3D|ImageViewer|VideoPlayer.cpp (T10)
Source/Viewers/DocumentWindow.cpp + App/ViewerOpening (T13)
Source/Cli/Commands.cpp   (T14: decode Scene branch + render command)
Examples/OnyxBox/         (T14: minimal scene entry kind)
.github/workflows/ci.yml  (T12)
DELETED at T11: Source/Rendering/{SceneRenderer,ShaderManager,GpuMesh,
  GridRenderer,AxisGizmo}.*, third_party/glad, Tools/OnyxOracle/HeadlessGL.*,
  Tools/OnyxOracle/AppConfigStub.cpp. KEPT (API-agnostic, move to
  Source/RenderVk or a neutral home): Camera.*, AnimationPlayer.*.
```

Execution order: T1→T8 (renderer core, strict), then T9, T10, T13, T14,
T11, T12. T13 is GL-independent and may slot anywhere after T8 if a
dispatch needs reordering; nothing else reorders.

---

### Task 1: Vulkan dependencies + VkContext boot

**Files:** root `CMakeLists.txt` (FetchContent: `Vulkan-Headers` tag
`v1.3.296`, `volk` tag `1.3.296`, `VulkanMemoryAllocator` tag `v3.1.0`,
`glslang` tag `14.3.0` with `ENABLE_OPT OFF`, `GLSLANG_TESTS OFF`; new
target `Onyx_RenderVk` + alias `Onyx::RenderVk`, links Onyx::Core, volk,
VMA headers; extend the source-list completeness check for the new dir);
Create `Include/Onyx/RenderVk/VkContext.h`, `Source/RenderVk/VkContext.cpp`.

**Interfaces (produces):**
```cpp
namespace Onyx::RenderVk {
struct ContextInfo { std::string deviceName; uint32_t apiVersion; bool validation; };
class VkContext {
public:
    // Headless-first: no surface needed. Picks a discrete GPU when
    // present, else the first device with graphics queue support.
    // Enables VK_KHR_swapchain only when presentSupport is requested.
    bool Init(bool presentSupport, std::string& err);
    void Shutdown();
    VkInstance Instance() const; VkPhysicalDevice Physical() const;
    VkDevice Device() const; VkQueue GraphicsQueue() const;
    uint32_t GraphicsFamily() const; VmaAllocator Allocator() const;
    const ContextInfo& Info() const;
};
}
```
Requirements: volk bootstraps (volkInitialize → instance → volkLoadInstance
→ device → volkLoadDevice); Vulkan 1.3 features chain
(`VkPhysicalDeviceVulkan13Features.dynamicRendering = VK_TRUE`,
`synchronization2 = VK_TRUE`) required — a device without them = Init
returns false with a clear err; VMA created with
`VMA_STATIC_VULKAN_FUNCTIONS 0` + volk function pointers; validation layer
`VK_LAYER_KHRONOS_validation` enabled in Debug IF enumerated, never
required. Every failure path releases what was created (RAII or explicit
teardown — match HeadlessGL's discipline).

Steps: TDD-shaped smoke — `onyx-oracle --vk-smoke` mode (mirrors
`--gl-smoke`): Init(false) → print `ContextInfo.deviceName` → Shutdown →
exit 0, or exit 77 with the err on stderr. ctest `VkBootSmoke`
(`SKIP_RETURN_CODE 77`, label `render`). Build, run (expect PASS locally,
device name printed). Full suite still green (count grows by 1). Commit:
`build(rendervk): Boot Vulkan 1.3 with volk and VMA`.

### Task 2: GPU resource primitives

**Files:** Create `Include/Onyx/RenderVk/VkResources.h`,
`Source/RenderVk/VkResources.cpp`; extend the tool smoke.

**Interfaces (produces):**
```cpp
namespace Onyx::RenderVk {
struct Buffer  { VkBuffer buf{}; VmaAllocation alloc{}; VkDeviceSize size{}; };
struct Image2D { VkImage img{}; VmaAllocation alloc{}; VkImageView view{};
                 VkFormat format{}; uint32_t width{}, height{}; };
class Resources {           // owns nothing global; all methods take VkContext&
public:
    static Buffer  CreateBuffer(VkContext&, VkDeviceSize, VkBufferUsageFlags,
                                VmaMemoryUsage, std::string& err);
    static Image2D CreateImage2D(VkContext&, uint32_t w, uint32_t h, VkFormat,
                                 VkImageUsageFlags, VkSampleCountFlagBits,
                                 std::string& err);
    static bool Upload(VkContext&, Buffer& dst, const void* data, VkDeviceSize,
                       std::string& err);          // staging + one-shot copy
    static bool UploadImage(VkContext&, Image2D& dst, const void* rgba,
                            std::string& err);     // staging + layout transitions
    static void Destroy(VkContext&, Buffer&);
    static void Destroy(VkContext&, Image2D&);
    // One-shot command scope: begin, record via callback, submit, wait.
    static bool OneShot(VkContext&, const std::function<void(VkCommandBuffer)>&,
                        std::string& err);
};
}
```
Layout transitions via `vkCmdPipelineBarrier2` (synchronization2). Smoke
extends `--vk-smoke`: create a 64x64 RGBA image, upload a checker,
round-trip is NOT required here (readback lands in T4) — creating,
uploading and destroying without validation errors is the bar. Debug build
run locally must show zero validation messages (capture via debug
messenger when validation is on; any message = test failure, printed).
Commit: `feat(rendervk): Buffers, images and staged uploads on VMA`.

### Task 3: SPIR-V pipeline plumbing + the PBR shader port

**Files:** Create `Source/RenderVk/Shaders/scene.vert`, `scene.frag`,
`background.vert`, `background.frag`, `grid.vert`, `grid.frag`,
`overlay.vert`, `overlay.frag` (GLSL 450); `cmake/ShaderCompile.cmake`
(function `onyx_add_spirv(target shader...)` — custom command running
glslang's standalone compiler `$<TARGET_FILE:glslang-standalone>` with
`--target-env vulkan1.3 -o out.spv`, then a tiny CMake `file(READ ... HEX)`
step generating `<name>_spv.h` with a `constexpr uint32_t` array; embedded,
no runtime file loading); Create `Include/Onyx/RenderVk/Pipelines.h`,
`Source/RenderVk/Pipelines.cpp`.

**Port mandate — read first, port faithfully:** the GLSL lives as raw
strings in `Source/Rendering/ShaderManager.cpp` (`SCENE_VERT` :45,
`SCENE_FRAG` :131, `GRID_*` :352+, `BG_*` :570+). Port SCENE vert/frag to
GLSL 450 preserving EXACTLY the lighting math, the `uShadingMode` semantics
(Solid vs Textured branch at :268 — the corpus pins both), the role
texture sampling, `uMetallic` use, the skinning weighted sum (4 lanes),
and the additive/subtractive blend expectations. Differences allowed ONLY
where the API demands (descriptor sets/push constants instead of uniforms,
`gl_Position` Y handling per the fixed convention). Document every
intentional divergence in a comment block at the top of scene.frag.

**Descriptor scheme (fixed here, consumed by T4-T8):** set 0 = per-frame
UBO (view, proj, camera pos, mode); set 1 = per-batch: UBO (material
factors: baseColor, layerColor, uvOffset, metallic, flags) + sampled
images for the 6 role slots (fixed array of 6 combined samplers, absent
roles bound to a shared 1x1 white/flat-normal default) + skinning palette
SSBO (std430, mat4[], bound even when unskinned with an identity entry).
Push constant: model matrix (64 bytes).

Tests (pure, no device): the generated `*_spv.h` arrays are non-empty and
start with the SPIR-V magic `0x07230203` — a unit test includes the
generated headers and asserts both, for every shader. Commit:
`feat(rendervk): Compile GLSL to embedded SPIR-V and port the scene shaders`.

### Task 4: Offscreen target + readback (HeadlessVk)

**Files:** Create `Include/Onyx/RenderVk/OffscreenTarget.h`,
`Source/RenderVk/OffscreenTarget.cpp`; extend oracle `--vk-smoke`.

**Interfaces (produces):** mirror HeadlessGL's shape so T7 can drive either:
```cpp
class OffscreenTarget {   // color RGBA8 + D32 depth, 4x MSAA + resolve
public:
    bool Create(VkContext&, int w, int h, std::string& err);
    void Destroy(VkContext&);
    // Begins dynamic rendering on the MSAA target, cleared to the given
    // color, depth cleared to 1.0, viewport/scissor set.
    void BeginFrame(VkCommandBuffer, const float clear[4]);
    void EndFrame(VkCommandBuffer);       // ends rendering + resolve + barrier
    bool Readback(VkContext&, std::vector<uint8_t>& rgbaTopDown, std::string& err);
};
```
Smoke: clear to a known color, readback, assert every pixel equals it
byte-exactly, WritePng via the existing stb path (move `WritePng` out of
HeadlessGL into a small shared `Tools/OnyxOracle/PngWrite.h` used by both).
Commit: `feat(rendervk): Offscreen MSAA target with byte-exact readback`.

### Task 5: SceneRendererVk — geometry, materials, blend

**Files:** Create `Include/Onyx/RenderVk/SceneRendererVk.h`,
`Source/RenderVk/SceneRendererVk.cpp`.

Public API mirrors the GL SceneRenderer where the oracle drives it:
`Build(const Parsers::SceneData&)`, `Render(VkCommandBuffer, const glm::mat4&
view, const glm::mat4& proj, ShadingMode, int w, int h)`,
`RenderBackground(VkCommandBuffer, top, bottom)`, `Clear(VkContext&)`,
`GetBatches()` returning the same `RenderBatch` bookkeeping fields the
report reads (vertexCount, triangleCount, blendMode, hasTexture, metallic,
materialColor, jointMap…) — the JSON report code must produce IDENTICAL
output for identical scenes (the report is renderer-agnostic by design;
reuse `Rendering::RenderBatch` as the bookkeeping struct or extract it to
a neutral header — implementer reads both and picks the minimal move,
stating it). Batch order MUST match GL's (meshParts iteration order; sky
first, then opaque, then additive — read `SceneRenderer::Build`'s sorting
before writing). Blend states: Normal = SRC_ALPHA/ONE_MINUS_SRC_ALPHA,
Additive = SRC_ALPHA/ONE, Subtractive/EnvMap per the GL code (read it).
Textures: pool uploaded once per Build; role→slot resolution reuses
`ResolveRoleIndices` (it is pure and public).

Test: `--vk-smoke` grows a mode `--vk-scene-smoke`: build the M0
blend-stack corpus scene, render Solid, readback, assert (a) not all
pixels equal the clear color, (b) two runs byte-identical. Commit:
`feat(rendervk): Scene geometry, role materials and blending`.

### Task 6: Skinning, grid, skeleton overlay

**Files:** modify `SceneRendererVk.*`; port `GridRenderer`/`AxisGizmo`
draw math into `Source/RenderVk/` equivalents (read the GL sources; the
grid/gizmo verts are procedural — port the generation, not the GL calls).

Skinning: palette computed with the SAME CPU code as GL —
`ComputeJointPalette`/`BuildBatchPalette` logic must be shared, not
re-implemented: extract those two (plus `BuildLocalTRS`) from
`SceneRenderer.cpp` into a pure, GL-free `Source/Rendering/JointPalette.{h,cpp}`
(namespace `Onyx::Rendering`, no GL includes) consumed by BOTH renderers —
this is the one edit allowed inside GL code before T9, because it moves
code without changing it (the GL golden gate `OracleReproducible` +
`OracleMatchesGolden` MUST still pass byte-identically after the move —
run them to prove it).

Test: `--vk-scene-smoke` extends to skinned-cube and joint-chain-200:
assert non-uniform output and run-twice byte-identity for each. Commit:
`feat(rendervk): Skinning palette, grid and skeleton overlay`.

### Task 7: Oracle parity — the milestone's teeth

**Files:** modify `Tools/OnyxOracle/Main.cpp` (+ CMake): `render-corpus
--renderer gl|vk` (default gl until T11), new command
`compare DIR_A DIR_B --max-channel-delta N --max-differing-pct P`
(PNG-aware: decode both PNGs — vendor `stb_image.h` beside the writer —
per-pixel per-channel |a-b|, fail if any channel delta > N OR the fraction
of pixels with any nonzero delta > P; JSONs compared byte-exact EXCEPT the
`pixelHash` line, which is expected to differ between renderers — compare
reports with that single line masked, and say so in --help).

ctest `VkOracleParity`: render corpus with `--renderer vk` into a work
dir, `compare` against `Tests/Golden/corpus` with the tolerances. TUNE the
tolerances on this machine: start at N=0/P=0, raise to the smallest values
that pass 3 consecutive runs, and REPORT the tuned values + the worst
per-scene deltas in the task report (they go into the ctest args and a
comment). STOP condition: if any scene needs maxChannelDelta > 8 or
differing-pct > 2%, something is semantically wrong (lighting/blend/pose
divergence, not rasterization noise) — report BLOCKED with the per-scene
numbers and the two PNGs' visual description instead of tuning past it.
Commit: `feat(oracle): Gate the Vulkan renderer against the GL goldens`.

### Task 8: RenderContext — the raw floor

**Files:** Create `Include/Onyx/RenderVk/RenderContext.h`,
`Source/RenderVk/RenderContext.cpp`.

```cpp
namespace Onyx::RenderVk {
struct FrameHandles { VkDevice device; VkQueue graphicsQueue;
                      VkCommandBuffer cmd; uint32_t graphicsFamily;
                      VmaAllocator allocator; };
class RenderContext {   // spec §8 raw floor
public:
    // Passes recorded in registration order after the scene, before UI.
    // The callback records into the frame's command buffer; exceptions
    // from it are contained (§7.1: logged, pass skipped this frame).
    int  AddPass(std::string name, std::function<void(const FrameHandles&)>);
    void RemovePass(int id);
    void Execute(const FrameHandles&);   // called by the frame owner
};
}
```
Test (device-gated): register a pass that records a
`vkCmdClearAttachments` tinting a 16x16 corner region inside the offscreen
frame; render blend-stack + the pass; readback asserts the corner differs
from the no-pass render and the rest is byte-identical. Plus a contained-
throw test: a pass that throws → logged, frame completes, other passes run.
Commit: `feat(rendervk): Raw-floor render context with contained passes`.

### Task 9: Shell swap 1 — window, swapchain, ImGui backend

**Files:** modify `Source/App/Window.cpp`, `Source/App/Platform/
Window_windows.cpp` (+linux), root CMake (`imgui_lib`: swap
`imgui_impl_opengl3.cpp` → `imgui_impl_vulkan.cpp`; imgui_lib links volk —
define `IMGUI_IMPL_VULKAN_USE_VOLK` so the backend uses volk loading),
`Examples/MinimalViewer` link line.

GLFW window: `GLFW_CLIENT_API = GLFW_NO_API`; surface via
`glfwCreateWindowSurface`; swapchain (FIFO present, RGBA8/BGRA8 as
available, recreate on resize/minimize-restore); per-frame: acquire →
record (scene offscreen targets → ImGui pass via dynamic rendering on the
swapchain image) → present, with 2 frames in flight and sync2 barriers.
ImGui init per `imgui_impl_vulkan`'s contract (descriptor pool, pipeline
cache nullptr, dynamic rendering path — `UseDynamicRendering = true`,
color format set). The Win32 borderless titlebar styling and DWM calls in
Platform/ are GL-agnostic — touch only the context/present parts. STOP
condition: if MinimalViewer does not open a window with ImGui rendering
(blank docked UI is enough at this task), report BLOCKED with the
validation output — do not stub past it.
Manual proof (screenshot impossible headless): run MinimalViewer with
smoke.obx for 5 seconds via a timeout script, assert exit is clean and the
log shows swapchain init + zero validation errors; capture the log tail
into the report. Commit: `feat(shell): Present through Vulkan and imgui_impl_vulkan`.

### Task 10: Shell swap 2 — viewport and viewer textures

**Files:** Create `Include/Onyx/RenderVk/TexturePool.h`,
`Source/RenderVk/TexturePool.cpp` (viewer-facing: upload RGBA → Image2D +
sampler + `ImGui_ImplVulkan_AddTexture` descriptor → `ImTextureID`;
Remove/flush with frame-latency safety — destruction deferred N frames);
modify `Source/Viewers/Viewport3D.cpp` (render via SceneRendererVk into an
OffscreenTarget whose resolve image is exposed as ImTextureID — replace
the GL FBO pair; MSAA + outline pass parity comes from the renderer),
`ImageViewer.cpp`, `VideoPlayer.cpp`, `Source/App/Panels/UiGallery.cpp`
(any GL texture uploads → TexturePool).

Tests: TexturePool logic that is pure (deferred-destruction bookkeeping:
N-frame delay queue) gets a unit test; the visual path is covered by the
same MinimalViewer timeout-run as T9 (now also opening the OnyxBox image
entry via a `--open-first-image` debug flag added to MinimalViewer for
this purpose — log line proves the ImageViewer drew a frame with a valid
ImTextureID). Commit: `feat(shell): Viewport and viewers on Vulkan textures`.

### Task 11: Delete GL — the point of no return

**Files:** delete `Source/Rendering/{SceneRenderer,ShaderManager,GpuMesh,
GridRenderer,AxisGizmo}.{h,cpp}` + their `Include/Onyx/Rendering/` headers,
`third_party/glad`, `Tools/OnyxOracle/HeadlessGL.*`,
`Tools/OnyxOracle/AppConfigStub.cpp`; move `Camera.*`, `AnimationPlayer.*`,
`JointPalette.*` into the RenderVk target's dirs (or keep path, retarget —
implementer picks the smaller diff and states it); rename target/namespace
`Onyx_RenderVk`/`RenderVk` → `Onyx_Render`/`Onyx::Rendering` (one
mechanical rename commit separate from the deletion commit); oracle:
`--renderer` flag loses `gl` (vk becomes default; `verify` byte-mode and
the GL goldens stay — they remain the parity anchor), `--gl-smoke`
removed, `VkOracleParity` still green.

MANDATES: the new renderer must NOT reference `AppConfig` anywhere —
`grep -rn "AppConfig" Source/RenderVk Source/Rendering` must be empty
after the move; that closes the carried link-completeness defect —
prove it by linking a Render-only consumer (onyx-oracle already is one,
now stub-free). LayerGuard manifests updated (Render layer now forbids
`glad|GL/|imgui|GLFW/` still — GLFW must NOT appear in Render sources;
the swapchain lives in Shell). Residue greps: `glad|glBindFramebuffer|
imgui_impl_opengl3|HeadlessGL` → zero hits outside docs/CHANGELOG.
Full suite green; `VkOracleParity` + repeats x3. Commits:
`refactor(render)!: Delete the GL renderer` then
`refactor(render): Fold RenderVk into the Render layer name`.

### Task 12: CI + docs — the milestone gate

**Files:** Create `.github/workflows/ci.yml`; modify CHANGELOG.md, roadmap.

CI (first CI in this repo — keep it minimal and honest):
- job `windows-msvc`: windows-latest, configure Ninja+MSVC, build, `ctest
  -j1` — render-labeled tests will exit 77 (no GPU) and report SKIP; the
  suite must still end green.
- job `linux-lavapipe`: ubuntu-latest, `apt-get install mesa-vulkan-drivers
  libvulkan1 ninja-build` (lavapipe ships in mesa-vulkan-drivers),
  `VK_ICD_FILENAMES` pointing at the lvp ICD json, build with gcc, `ctest
  -j1` — render tests RUN on lavapipe, `VkOracleParity` gates every PR.
  FFmpeg: pass `-DONYX_COMPONENT_MEDIA=OFF` if the Linux fetch path is not
  already proven in this repo (check how CMake handles it; do not fight
  FFmpeg in this task — Media is optional by design).
- NOTE in the workflow header + task report: this repo's remote has never
  received these branches (push is a pending human action) — the workflow
  is authored and locally lint-checked (`python -c "import yaml,sys;
  yaml.safe_load(open('.github/workflows/ci.yml'))"`), and the lavapipe leg
  runs for real only after the first push. Tolerances may need one bump
  for lavapipe rasterization differences: leave the ctest tolerance args
  in ONE cmake variable (`ONYX_PARITY_ARGS`) so that bump is a one-line PR.
CHANGELOG: M4 section (Vulkan renderer, GL deleted BREAKING, CI). Roadmap:
M4 marked DONE with the lavapipe caveat named honestly; carried list
statuses updated (link-completeness fixed at T11, per-container render at
T14, Scene decode branch at T14, tabs/async at T13).
Commit: `ci: Build on Windows and gate render parity on lavapipe` +
`docs: Record the M4 Vulkan milestone`.

### Task 13: Carried Shell work — document-close tabs + async decode

**Files:** modify `Source/Viewers/DocumentWindow.cpp/.h`,
`Include/Onyx/App/ViewerOpening.h` + `Source/App/ViewerOpening.cpp`,
`Source/App/App.cpp` (wiring), Tests.

(a) `(DocumentId, tab)` association: every tab opened through
`OpenSelection` records its DocumentId; subscribe (RAII, member order per
the Window destruction contract) to `EventDocumentClosed` and close every
tab belonging to that document — restoring the legacy close-on-WadClosed
behavior the M3b ledger tracked as an inert regression. (b) Async decode:
`OpenSelection` moves the decode off the UI thread via `Jobs().Submit` on
a `decode` lane, completing on Pump with the viewer opened then — the
loading state is a simple "Decoding…" tab placeholder; cancellation on
document close (cooperative — the M2 JobQueue contract). Salvage rules
unchanged (Failed nodes never decode; null decode → LOG_WARN path stays).
Tests: seam-level like M3b's — (DocumentId,tab) bookkeeping pure logic
unit-tested; async decode through a stub decoder that blocks until
released, asserting no decode on the caller thread and close-cancels.
Commit: `feat(shell): Close a document's tabs and decode off the UI thread`.

### Task 14: Carried CLI work — Scene decode branch + render command

**Files:** modify `Source/Cli/Commands.cpp/.h`, `Examples/OnyxBox/
OnyxBoxModule.cpp` (+ tests), `Tools/OnyxOracle` untouched.

(a) OnyxBox gains entry kind 3 `mesh`: payload = 12 floats (baseColor RGBA
+ position xyz + half-extents xyz + 2 floats padding) parsed into a
single-part cube `SceneData` by a registered Scene decoder — the minimal
honest Scene capability (hardening: payload size check → Failed + diag).
(b) `CmdDecode` gains the Scene branch mirroring ViewerRouting's priority
(Scene > Image > Text — spec §11: CLI and GUI must not diverge): for a
scene entry, print a one-line summary (parts/materials/vertices) and with
`--out` write the render report JSON (reuse `BuildReport`? it lives in the
oracle — NO: keep CLI text-only summary, no oracle dependency; state this
boundary in the code comment). (c) New command `render <container> <entry>
--out out.png [--width N --height N]`: probe → open → resolve entry by
name (first match, documented) → Scene decode → SceneRendererVk +
OffscreenTarget → PNG + report JSON beside it; exit 77 when no Vulkan
device (consistent with the tool convention), kNoModule/kUsage semantics
per Commands.h. Tests: obx fixture with a mesh entry → decode summary
asserts counts; render is device-gated (77-skip ctest) asserting the PNG
exists, is non-uniform, and two runs are byte-identical.
Commit: `feat(cli): Decode scenes and render containers headlessly`.

---

## Milestone Gate (roadmap M4, restated honestly)

1. Full serial ctest green locally, `VkOracleParity` + `OracleReproducible`
   x3 repeats green, with tuned tolerances documented.
2. GL gone: residue greps zero; `Onyx_Render` (post-rename) is
   link-complete standalone with zero stubs — the M1-era defect closed.
3. Shell runs on Vulkan: MinimalViewer timeout-run clean with zero
   validation errors (log captured); Jean's GUI eyeball is FLAGGED PENDING
   (he is offline) — recorded in the roadmap, not skipped silently.
4. CI authored + YAML-validated; lavapipe leg honestly marked
   pending-first-push (no remote push is authorized).
5. Carried list all landed: tabs/async (T13), Scene branch + render (T14),
   link-completeness (T11), per-container render (T14).

## Self-review notes

- Interface names cross-checked against the tree: `Parsers::SceneData`,
  `ShadingMode` (Rendering/ShaderManager.h), `ResolveRoleIndices` (public
  static, pure), `RenderBatch` fields, `IFile`, JobQueue lanes (M2),
  EventDocumentClosed exists (M3b kept it? — T13's implementer must verify
  the exact event name in Include/Onyx/Services/Events.h and use what is
  there; if only DocumentClosed exists under another name, use it — the
  brief's name is descriptive, the header is authoritative).
- Deliberately open points are owned by tasks with read-first mandates:
  GL blend-state exact factors (T5 reads SceneRenderer), batch sort (T5),
  palette CPU path (T6 extracts, byte-gate proves the move), imgui vulkan
  init details (T9 reads the backend header's contract comment block).
- Version pins (volk/Headers 1.3.296, VMA 3.1.0, glslang 14.3.0) chosen as
  current stable at plan time; T1's implementer verifies the tags exist at
  fetch and reports any substitution (same-major nearest) as a disclosed
  deviation.
- The GL goldens never regenerate in M4 (frozen constraint) — the only
  task allowed to touch GL sources before deletion is T6's pure code MOVE,
  gated by the byte-identity ctests.
