# OnyxSDK rendering layer survey (read-only)

Scope: Source/Rendering (10 .cpp + Shaders), Include/Onyx/Rendering (13 .h), Source/Viewers/Viewport3D.cpp, Source/Cli/Render.cpp, plus Source/App/Window.cpp (frame loop only) and Source/App/TexturePool.cpp (lifetime only).
Note: `hooks/code_tree.py` does not exist in the repo; symbol map was built by reading the files.
Paths are relative to D:\CodingProjects\OnyxSDK.

## 0. Size

| Part | LOC |
|---|---|
| Source/Rendering/*.cpp | 4360 (+683 LOC GLSL in Source/Rendering/Shaders) |
| Include/Onyx/Rendering/*.h | 2122, of which 1277 (60%) are comment lines |
| Viewport3D.cpp | 1153 |
| Cli/Render.cpp | 448 |
| Total rendering-related | ~8100 |

Comment archaeology: 41 hits of "T10 fix-round-1", "F5 fix", "M5 Task 7", "Task N" inside Source/Rendering + Include/Rendering + Viewport3D. Files carry task-tracker history (VkContext.cpp:32-48, 55-79, 202-217; SceneRendererVk.h:1-70; Viewport3D.cpp:1-106) instead of intent. 12 hits reference GOW/PS2/Q.14 (game-specific) in a "game-agnostic" layer.

## 1. Resource lifecycle

Owners:
- VkInstance/VkDevice/VmaAllocator: `VkContext` (VkContext.cpp:96-393 Init, 395-420 Shutdown). Manual Init/Shutdown, not RAII. Init calls Shutdown() on every failure path (OK). Shutdown order allocator, device, messenger, instance is correct. No vkDeviceWaitIdle in Shutdown (callers must do it).
- Process-global singleton: `SetGlobalContext/GetGlobalContext` (VkContext.cpp:440-445), 18 call sites. Viewport3D, TexturePool etc. fetch the context from a global instead of receiving it.
- Pipelines: `Pipelines::Create*/Destroy` static functions on plain structs of raw handles (Pipelines.h). Manual pairs, no RAII. Each Create has 5-8 hand-written cleanup branches.
- Buffers/images: `Resources::CreateBuffer/CreateImage2D/Destroy` (VkResources.cpp:54-342) on plain structs `Buffer`/`Image2D`, no destructors. Every owner needs a hand-written Clear(ctx) (SceneRendererVk.cpp:1047-1120, 73 lines of Destroy calls).
- Descriptor pools: SceneRendererVk creates up to FOUR separate VkDescriptorPools: main (:200), bg (:746), grid (:813), overlay (:975), the last three lazily inside Render* calls.

Destruction-order / leak problems:
1. **GPU use-after-free on viewport resize (real bug).** `Viewport3D::ResizeTarget` (Viewport3D.cpp:340-374) calls `m_vk->target.Destroy()` (immediate vmaDestroyImage + vkDestroyImageView, OffscreenTarget.cpp:88-97) with no wait and no deferral, while the previous frame's ImGui command buffer may still be sampling that resolve view. Ordering: ImGui Draw runs first, `presentFrame()` fence wait comes later (Window.cpp:1007), kFramesInFlight=2, so up to 2 frames can still reference the old image. The descriptor is retired through DeferredDestroyQueue with a 2-frame delay (TexturePool.cpp:252-261) but the image it points to is not. Triggers on any dock/window resize drag.
2. **Silent leak at shutdown**: Viewport3D::~Viewport3D destroys GPU objects only `if (live && m_vkReady)` where live = GetGlobalContext() (Viewport3D.cpp:201-209). Window::exitVulkan clears the global first (see TexturePool.cpp dtor comment ~line 123-137), so a still-open viewer leaks every pipeline/buffer/image, then vmaDestroyAllocator runs with live allocations. TexturePool dtor does the same "deliberately leak".
3. **Leak on lazy-init failure**: RenderBackground/RenderGrid/RenderSkeleton create pool, then buffer, then set; if a later step fails they return false without resetting (SceneRendererVk.cpp:746-777, 813-844, 975-1006). `m_bgSet` stays NULL so the next call re-enters and overwrites `m_bgPool`/`m_bgBuf`, leaking the first pair.
4. `BuildBatch` failure paths: 4 hand-copied cleanup blocks (SceneRendererVk.cpp:313-327, 409-413, 432-448, 462-466); any new field needs 4 edits.
5. `SceneRendererVk::Clear` has no wait; safe only because every submission blocks (section 2). Destructor is `= default` and does NOT free GPU resources; forgetting Clear(ctx) leaks (SceneRendererVk.h:119-120).

vkDeviceWaitIdle: Window.cpp:229, 608, 804 (init/swapchain recreate/exit), TexturePool.cpp:146 (dtor). None inside Source/Rendering and none per frame. Acceptable. The real band-aid is architectural: `Resources::OneShot` fence-wait after every submit.

## 2. Frame structure

Two separate GPU paths on ONE VkQueue (VkContext.cpp:330-364):

A. **Swapchain path (Window.cpp:999-1182)**: 2 frames in flight, per-frame fence/cmd buffer/imageAvailable semaphore, per-image renderFinished semaphore, imagesInFlight tracking, FIFO present (Window.cpp:501), sync2 barriers, dynamic rendering, recreate on OUT_OF_DATE with vkDeviceWaitIdle (Window.cpp:608). Textbook and fine. RenderContext::Execute passes run inside this render scope (Window.cpp:1096).

B. **Offscreen 3D path (Viewport3D::RenderFrame, Viewport3D.cpp:376-445)**: runs from inside `ImGui::Draw()` on the UI thread. `Resources::OneShot` (VkResources.cpp:344-438) creates a VkCommandPool, allocates a cmd buffer, records, creates a VkFence, vkQueueSubmit2, `vkWaitForFences(UINT64_MAX)`, destroys fence+pool. Per 3D redraw: 1 pool create/destroy, 1 fence create/destroy, 1 blocking CPU wait; CPU and GPU are serialized. Viewport3D.cpp:19-39 admits it ("reasonable follow-up").
- `m_needsRedraw` gating hides this only when idle. Playing an animation sets it every frame (Viewport3D.cpp:517), so animated viewing = one blocking submit per frame.
- The renderer's correctness rests on this stall: SceneRendererVk.h PRECONDITION (~lines 49-66) says every UBO/SSBO/VBO is single-buffered, host-visible, CPU-written without barriers or per-frame copies, "safe only because OneShot blocks". Pipelining breaks 7+ buffers silently (frame UBO main/sky, bg UBO, grid UBO, overlay UBO+VBO, per-batch joint SSBO).

MSAA: 4x color + D32 depth + 1x resolve via dynamic-rendering resolveMode AVERAGE (OffscreenTarget.cpp:160-199). Correct. Nits: MSAA images are DONT_CARE-stored but not TRANSIENT/lazily allocated (wastes memory on tiled GPUs); `kSampleCount`, formats are global constants baked into every pipeline; D32_SFLOAT support not queried.

Barriers: begin = 3 barriers (color/depth from UNDEFINED each frame, resolve conditional), end = 1 (to TRANSFER_SRC, OffscreenTarget.cpp:221-240), then PrepareForSampling = 1 more (to SHADER_READ). The TRANSFER_SRC hop exists only for the headless Readback; the interactive path pays two transitions where one would do.

Descriptor rebuilds per frame: none (good). Per-frame CPU allocations:
- RenderSkeleton builds a fresh `std::vector<OverlayVertex>` and reserves up to 16*joints entries every redraw (SceneRendererVk.cpp:905-952).
- Animated frame: `ComputeJointMatrices` allocates 3 mat4 vectors (AnimationPlayer.cpp:393-); `UploadBatchPalettes` allocates a `std::vector<mat4>` per batch via BuildBatchPalette and rewrites every batch SSBO (SceneRendererVk.cpp:720-732).
- Per-clip: `EnsureBaked` bakes the whole clip synchronously on the UI thread on SetAnimation (AnimationPlayer.cpp:204-291); `m_bakeCache` grows unbounded.

Per-load stalls: `SceneRendererVk::Build` -> every `Resources::Upload`/`UploadImage` is its own OneShot (own pool + fence + submit + wait) plus its own staging VMA allocation. Per mesh part: vertex + index + material UBO = 3 blocking submits; per texture 1 more; +1 for the 1x1 default (SceneRendererVk.cpp:103-132, 308-327, 404-407). 100 parts / 50 textures = ~350 sequential queue round-trips on the UI thread. LoadScene runs it synchronously (Viewport3D.cpp:268-274). No batching, no staging reuse, no transfer queue.

Readback: staging is GPU_TO_CPU but `CreateBuffer` sets only HOST_ACCESS_SEQUENTIAL_WRITE for every host usage (VkResources.cpp:16-27, 70-73). For CPU-read buffers VMA requires HOST_ACCESS_RANDOM_BIT (and invalidate on non-coherent memory). Works only because legacy GPU_TO_CPU usually lands in cached memory. Also uses deprecated VMA usage enums (CPU_ONLY, CPU_TO_GPU, GPU_ONLY).

## 3. Pipeline management

- 7 VkPipelines: scene x4 variants (opaque, blendNormal, blendAdditive, sky), grid, background, overlay; 4 pipeline layouts. Four hand-written creators: CreateScene 212 LOC (Pipelines.cpp:112-323), CreateGrid 136 (:338-473), CreateBackground 134 (:482-615), CreateOverlay 156 (:624-779).
- Duplication measured: after name normalisation CreateGrid vs CreateBackground differ in 24 of ~134 lines (82% identical). Each repeats set layout, pipeline layout, 2 shader modules, input assembly/viewport/raster/MSAA/dynamic/depth/blend structs and cleanup. Roughly 400 of 788 lines removable with one builder helper.
- Keying: none; fixed named structs. A new variant (wireframe, picking, two-sided) means editing struct + Create + Destroy + renderer buckets.
- **No VkPipelineCache** (`vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, ...)`, Pipelines.cpp:311); pipelines created one at a time.
- Shaders: compiled at BUILD time via glslang-standalone -> SPIR-V -> generated header (cmake/ShaderCompile.cmake:20-50). Good (no runtime compile). Drawback: behaviour switches are runtime branches on `uFlags` (scene.vert FLAG_USE_JOINTS etc.), not specialization constants; unskinned batches still bind a dummy 1-mat4 SSBO; the fragment shader carries all 7 sampler slots.
- scene.frag header lists 5 "divergences from GL" referring to a deleted GL renderer (scene.frag lines ~1-60). GL leftovers: `ShadingModeInt` collapses 6 enum values to {0,2} (SceneRendererVk.cpp:38-49); Matcap/Wireframe/TexturedWire enums are no-ops (RenderBatch.h:37-56); `RenderBatch::gpuMesh` is a dead `shared_ptr<GpuMesh>` for a type that no longer exists (RenderBatch.h:~63, SceneRendererVk.cpp:579-581); `isHighlighted` never read; texture0..texScatter uint32 "sentinels" exist only to keep a GL struct alive; `kTextureRoleCount=10` vs 7 bound roles.
- Duplicated orchestration:
  - Interactive (Viewport3D::EnsureVulkanReady/RenderFrame, Viewport3D.cpp:315-445) vs headless (RenderToImage.cpp:30-106) are two hand-written copies of: create pipelines -> create target -> Build -> OneShot{BeginFrame, bg, Render, EndFrame}. Viewport3D.cpp:84-103 explicitly justifies not reusing RenderToImage.
  - RenderToImage rebuilds scene (4) + bg pipelines and 4 shader modules per call, and the no-ctx overload runs a full `VkContext::Init` (new instance+device+VMA) per call (RenderToImage.cpp:109-126). CLI renders one entry per process but Cli/Render.cpp:361 still pays full init per CmdRender.
  - RenderToImage cannot draw grid or skeleton: headless output != viewport output.
  - Camera framing/bounds math in 3 copies: Cli/Render.cpp:325-354, Viewport3D::ComputeBounds (Viewport3D.cpp:277-313), Rendering/Camera.cpp FocusOn. FOV 55 deg duplicated (Cli/Render.cpp:339).
  - Opposite projection conventions: SceneRendererVk::Render asserts `proj[1][1] < 0` (SceneRendererVk.cpp:540, caller applies VulkanProjection), RenderToImage rejects `proj[1][1] <= 0` (RenderToImage.cpp ~line 40, applies it internally).
  - `RenderContext` (60 LOC cpp + 105 h): std::function pass list with raw VkDevice/VkQueue/VkCommandBuffer/VmaAllocator. Only created/executed in Window.cpp:336, 1096; grep finds NO AddPass caller anywhere; Viewport3D explains it cannot use it (Viewport3D.cpp:19-39). Dead extension point.

## 4. Data model (mesh / material / skinning)

- `Parsers::SceneData` (SceneNode.h:54-92): meshParts (CPU `vector<GpuVertex>` + `vector<uint32_t>`), materials (`MaterialDesc` with `std::map<TextureRole,int>`), textures (`unique_ptr<TextureData>`), skeleton, animations. Vulkan-free: good. Not game-agnostic: GOW2/GOWR comments (SceneNode.h:58-62, MeshData.h:20-21 `isRigid // GOWR`, `meshHash // GOWR LOD-blob`), `flipZ` is a per-game flag on the generic struct.
- `Domain::GpuVertex` (MeshVertex.h) = 26 floats = 104 bytes/vertex always (pos3, normal3, uv2, color4 float, uv1 2, weights4 float, indices uvec4 32-bit, tangent4), even for static meshes. The "Gpu" type is the parser's output type and Pipelines.cpp:45-68 uses offsetof on it: parsers emit the exact GPU layout.
- Vertex copies: parser CPU copy (retained forever in Viewport3D `m_sceneData`, needed only for bounds/counts) -> per-upload staging memcpy -> GPU_ONLY buffer. 3 copies, staging allocated+freed per upload. Textures: CPU RGBA (retained) -> staging -> GPU image with GPU mip blit chain (good).
- Materials: not a shared role-indexed table. Each MeshPart gets its OWN descriptor set, material UBO (GPU_ONLY, uploaded by OneShot), SSBO and 7 combined-image-sampler slots written individually, falling back to a 1x1 white (SceneRendererVk.cpp:453-508). No dedupe by materialId. Pool sized `batchCap * 7` samplers (:189-193). Height/Detail/Emissive roles are resolved then dropped (:370-373).
- Per-part vertex+index buffers and a descriptor set rebind per draw (DrawBatch :517-524): N parts = N VBs, N IBs, N sets; no merging.
- Skinning: per-batch joint SSBO, CPU_TO_GPU persistently mapped (SceneRendererVk.cpp:427-451), shader reads unsized `mat4 uJoints[]` by `aBoneIndices.xyzw` (scene.vert). No hardcoded max joints (good). But:
  - **No bounds protection**: SSBO size is exactly `jointMap.size()` mat4s (:419-428); vertex bone indices come straight from parser data and are never validated against it; robustBufferAccess/robustness2 is never enabled (VkContext.cpp:336-350 enables only dynamicRendering + synchronization2). Bad index = OOB SSBO read (UB / device lost on some GPUs). BuildBatchPalette guards its own writes only (JointPalette.cpp:103-115).
  - `UploadBatchPalettes` silently truncates a palette larger than the SSBO (SceneRendererVk.cpp:728).
- Skeleton/animation decoding hardcodes the source game format: Q.14 fixed point (`1<<14`), euler-or-quat per joint, `vectors4/5/6`, AnimSamplesManager/substreams (JointPalette.cpp:11-70, AnimationPlayer.cpp:293-528). The euler/quat conversion block is written twice verbatim (JointPalette.cpp:25-55 and AnimationPlayer.cpp ~450-475) plus a third `toQuat` (~331). Format logic living in `Onyx::Rendering`; a generic renderer should receive baked `mat4` palettes from a format module.
- `ComputeJointMatrices` indexes `m_jointLocalRot[i]` for all skeleton joints without checking the baked array size (AnimationPlayer.cpp ~410-420).
- scene.vert: `vColor = aColor * 2.0` is a PS2 vertex-colour convention baked into the generic shader.

## 5. Public API / the "two floors"

Design (Viewport3D.cpp:84-103): "ready floor" (RenderToImage) vs "raw floor" (VkContext + Pipelines + SceneRendererVk + OffscreenTarget + RenderContext). Reality:
- The raw floor is the whole usable public API (Include/Onyx/Render.h, Onyx.h:80-83). Nearly every public header includes volk.h and vk_mem_alloc.h (RenderContext.h:9-10, VkContext.h, VkResources.h, Pipelines.h, ...), with a documented binding include-order rule (VkContext.h:1-5). Viewport3D.h hides it behind unique_ptr<VulkanState> because including them dragged <windows.h> and broke `DecodedText` (Viewport3D.cpp:12-22). Consumers must adopt volk + VMA + Vulkan headers and a macro-collision workaround.
- Vulkan types in public signatures: `VkContext&` everywhere; `VkCommandBuffer` in Render/BeginFrame/EndFrame/RenderBackground/RenderGrid/RenderSkeleton; VkDescriptorSetLayout/VkPipeline/VkPipelineLayout in ScenePipelines/GridPipeline/BackgroundPipeline/OverlayPipeline; VkImageView in OffscreenTarget::ResolveView; VkDevice/VkQueue/VmaAllocator in FrameHandles; kColorFormat/kDepthFormat/kSampleCount constants.
- SceneRendererVk: `Render*` methods lazily CREATE GPU resources mid-record; `Clear(ctx)` required before destruction; raw non-owning `m_pipelines`; bundles AnimationPlayer; exposes mutable `GetBatches()` that the inspector writes `isVisible` into. Renderer == scene graph == animation host == overlay drawer.
- Verdict: the split is not clean. The "ready" floor is incomplete (no grid/skeleton/animation, per-call pipeline+device rebuild, opposite projection convention). The "raw" floor exposes Vulkan, volk, VMA and a blocking-submit model to every consumer.

## 6. Viewport3D.cpp (1153 LOC)

Owns (Viewport3D.cpp:144-210): 4 pipeline sets, SceneRendererVk, OffscreenTarget, TexturePool (display ImTextureID), VkContext pointer from global, SceneData, bounds, Camera (+ AppConfig sync :165-176), AxisGizmo, animation transport bar + clip selector + timeline (:796-~940), inspector with mesh batch tree and LOD grouping (:~940-1153), toolbar (:638-749), keyboard shortcuts (:584-636), global active-animation registration (:195, :506), background-colour policy from AppConfig (:379-394), EventAnimationLoaded post (:259-261).
No picking exists (isHighlighted is set by the inspector and never rendered).
Verdict: god object: ImGui panel + render orchestrator + GPU resource owner + animation UI + inspector + config reader. Specifics:
- Render orchestration inside a UI class; testable only through the full app.
- `LoadScene` builds synchronously and only if Vulkan is ready at that instant (Viewport3D.cpp:268); if the global ctx is null, Draw() only retries EnsureVulkanReady (:451), never Build, so the scene is kept but never uploaded: blank viewport, no error. Build failure is log-only (:271-273).
- `Draw()` does animation update, camera update, resize, render, input, toolbar, overlays in one function (:447-582).
- Dead code: `LoadFromMeshData` stub (:236-251).

## 7. Bug smells / limits

Ranked bugs:
1. Resize UAF of in-flight image (Viewport3D.cpp:345-347 -> OffscreenTarget.cpp:88-97).
2. Unvalidated bone index -> OOB SSBO read with no robustness feature (SceneRendererVk.cpp:419-428, VkContext.cpp:336-350).
3. Lazy-init leak/reentry in RenderBackground/Grid/Skeleton (SceneRendererVk.cpp:746-777, 813-844, 975-1006).
4. LoadScene never retries Build when Vulkan not ready (Viewport3D.cpp:268-274, 451).
5. Readback buffer uses SEQUENTIAL_WRITE (VkResources.cpp:70-73).
6. UI-thread blocking everywhere: Build (O(parts) submits), RenderFrame (every redraw), animation bake (sync).
7. Single shared VkQueue with no mutex: Window.cpp submits/presents on `GraphicsQueue()` and OneShot submits on the same queue; safe only while single-threaded. Any async loader calling `Resources::Upload` from a worker violates external synchronization.
8. `UploadImage` memcpy's `width*height*4` from `rgba` (VkResources.cpp:208, 225) while `TextureData::IsValid` checks only `!pixels.empty()` (Parsers/TextureData.h:20): OOB read on malformed parser output.
9. Device selection: first discrete GPU, no override, only dynamicRendering+sync2 checked (VkContext.cpp:272-326); non-Windows present support unchecked at selection (VkContext.cpp:50-92 comment admits "never run against a real window").

Magic numbers / limits: kFramesInFlight=2, 4x MSAA, RGBA8_UNORM/D32 fixed; skeleton overlay sizes 0.05/0.02/0.04 world units (SceneRendererVk.cpp ~901-945) and 16 verts/joint capacity (:158); light dir/normal strength compile-time constants (scene.frag note 5, no lighting API); UI heights 86/40; FOV 55 duplicated. No max-joints/textures/materials limits (pool sized from scene). Validation auto-enabled in debug incl. sync validation (VkContext.cpp:155-226) but the callback only counts messages (VkContext.cpp:422-438).
TODO/FIXME: one literal TODO (VkContext.h:107). Debt lives as prose ("Known gaps", "disclosed perf gap", "follow-up") in comments. No commented-out code; large explanatory blocks instead.

## 8. Weight and minimal core

Current ~8100 LOC for: Vulkan bootstrap, 7-pipeline forward renderer with MSAA offscreen, a game-specific animation baker, and a UI-embedded orchestrator.

Minimal viable core for an asset-viewer SDK (~1500-2000 LOC realistic):
1. RAII `GpuDevice` (instance/device/VMA/queue, mutex-protected submit) ~300.
2. `UploadContext`: persistent staging + one command buffer per scene load, single fence ~200 (replaces OneShot-per-buffer).
3. Mesh (one VB/IB per scene with draw ranges), Material (storage buffer + descriptor-indexed textures), Texture RAII ~400.
4. One `GraphicsPipelineBuilder` + `PipelineCache` keyed on a small POD state, specialization constants ~250.
5. RenderTarget (MSAA+resolve, transient) + per-frame resources (N frames in flight) ~350.
6. SceneRenderer taking view/proj + draw list; generic debug-draw for grid/bg/lines ~300.
7. Skinning input = pre-baked mat4 palette; format decoding in the format module.

Beyond core today: game-specific skeleton/anim (JointPalette.cpp 116 + AnimationPlayer.cpp 528 + .h 151 ~ 800 LOC); RenderContext (~165 LOC, zero users); GL remnants (gpuMesh, Matcap/Wireframe/TexturedWire, isHighlighted, 3 of 10 roles, LoadFromMeshData); RenderToImage as a second orchestration (200 h + 128 cpp); ~400 duplicated pipeline LOC; 3 duplicated lazy-pool blocks in SceneRendererVk (~200 LOC); ~370 lines of inspector/transport UI in Viewport3D; ~1900 lines of task-history comments.

## 9. Recommendations

1. Fix the resize UAF now: retire the old OffscreenTarget through DeferredDestroyQueue (or delay by kFramesInFlight).
2. Replace OneShot-per-upload with a batched UploadContext; scene Build becomes one submit (largest load-time win; prerequisite for async load).
3. Decide the frame model: blocking-but-batched (document and assert the PRECONDITION) or per-frame ring buffers. Do not leave the half state.
4. Move-only RAII wrappers for Buffer/Image2D/Pipeline/DescriptorPool; delete Clear(ctx)/Destroy(ctx) pairs.
5. Pipeline builder + VkPipelineCache (-400 LOC); delete RenderContext and GL remnants.
6. Move animation/skeleton decoding out of Rendering; renderer receives palettes.
7. Make RenderToImage drive the same SceneRenderer/pipelines; stop creating a VkContext per call; unify projection convention.
8. Hide Vulkan from generic public headers (facade/pimpl) or ship it as an explicit advanced module.
9. Split Viewport3D into ViewportPanel (UI), SceneView (renderer+target+camera), Inspector panel, AnimationTransport panel; retry Build when Vulkan becomes ready; surface Build errors in the UI.
10. Enable robustBufferAccess or validate bone indices at parse time.
