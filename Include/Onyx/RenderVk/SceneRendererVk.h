#pragma once

// See VkContext.h for the binding include-order rule (volk.h, then
// vk_mem_alloc.h, before any other Vulkan-touching header). VkResources.h
// / Pipelines.h already pull both in (via VkContext.h) in that order, so
// including them first here keeps the rule honored without repeating it.
#include <Onyx/RenderVk/Pipelines.h>
#include <Onyx/RenderVk/VkResources.h>

// ═══════════════════════════════════════════════════════════════════════
// RenderBatch reuse (task-5 brief's "bookkeeping question"; the shared
// struct moved homes at Task 11 -- see Include/Onyx/Rendering/RenderBatch.h's
// own top comment for the full story, this note keeps the parts still
// load-bearing here): Tools/OnyxOracle/RenderReport.{h,cpp} ALREADY takes
// std::vector<Rendering::RenderBatch> directly, and RenderBatch.h only
// forward-declares `GLuint` as `using GLuint = unsigned int;` -- it does
// NOT include glad, so pulling it in here does not violate Onyx_RenderVk's
// "no GL calls/headers" rule (verified: no GL header, no GL function call,
// just a plain integer typedef reused as a bookkeeping field).
// RenderBatch's only non-trivial member is `std::shared_ptr<GpuMesh>`,
// which SceneRendererVk never constructs (it stays null-initialized;
// Vulkan geometry lives in this file's own GpuBatch, not in a GpuMesh) --
// `GpuMesh` is now forward-declared only and never defined anywhere in the
// codebase (its GL implementation was deleted alongside the rest of the GL
// renderer at Task 11), which this file never notices since it only ever
// holds a null shared_ptr to it.
//
// Given that, the minimal move is to reuse Rendering::RenderBatch
// verbatim rather than extract a lookalike struct: BuildReport()'s output
// shape is pinned to RenderBatch's exact field set already, so a second
// struct would only add a translation step (and a second place these
// fields could drift out of sync) for zero benefit -- the oracle's JSON
// report for a given scene comes out byte-identical whether the (now
// deleted) GL SceneRenderer or this file populated the vector, which was
// exactly the parity property task 5 existed to establish and Task 7's
// VkOracleParity still leans on today.
//
// The `GLuint texture0/texture1/texNormal/texAO/texGloss/texScatter`
// fields are populated here as plain nonzero/zero SENTINELS (never a real
// GPU handle of any kind) -- their only consumers are RenderReport's
// CountBoundRoleTextures (nonzero => bound) and this struct's own
// hasTexture/hasEnvmap booleans, so a sentinel preserves the report's
// semantics exactly without smuggling a GL concept into a Vulkan
// translation unit. The real Vulkan-side texture/sampler state
// (VkImageView + VkSampler, wired into each batch's descriptor set) lives
// in this file's private GpuBatch, never in RenderBatch.
// ═══════════════════════════════════════════════════════════════════════
#include <Onyx/Rendering/RenderBatch.h> // Rendering::RenderBatch, Rendering::ShadingMode, Rendering::ResolveRoleIndices

#include <Onyx/Parsers/SceneNode.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Onyx::RenderVk {

/// Vulkan port of Onyx::Rendering::SceneRenderer (GL) -- builds GPU
/// resources from a Parsers::SceneData and draws them through T3's
/// ScenePipelines into whatever dynamic-rendering target the caller has
/// begun (typically a T4 OffscreenTarget between BeginFrame/EndFrame).
///
/// API surface mirrors GL's SceneRenderer where the oracle drives it
/// (Build/Render/RenderBackground/Clear/GetBatches), with one structural
/// difference GL's implicit global context does not have to make
/// explicit: every method that touches the GPU takes the VkContext (and,
/// for Build, the caller-owned ScenePipelines it must draw through) as an
/// explicit parameter, following the same convention OffscreenTarget
/// already established (Create/Destroy/Readback all take `VkContext&`;
/// only BeginFrame/EndFrame, which just record onto an already-open
/// command buffer, do not). Render() needs no VkContext because by the
/// time it runs, Build() has already created every GPU resource it reads
/// and cached everything it needs (including a raw pointer into each
/// frame UBO's persistently-mapped memory) -- it only records commands.
///
/// Not copyable: owns live GPU resources (buffers, images, descriptor
/// pools) exactly like OffscreenTarget/VkResources' Buffer/Image2D.
/// Caller-owned: Clear(ctx) must run before destruction (this class holds
/// no ctx of its own to destroy itself with) and before the ScenePipelines
/// or BackgroundPipeline passed to Build/RenderBackground are destroyed --
/// this object only borrows references to them.
class SceneRendererVk {
public:
    SceneRendererVk() = default;
    ~SceneRendererVk() = default;

    SceneRendererVk(const SceneRendererVk&) = delete;
    SceneRendererVk& operator=(const SceneRendererVk&) = delete;

    /// Builds GPU resources (vertex/index/UBO/SSBO buffers, textures,
    /// descriptor sets) for every non-empty mesh part in `scene`, in
    /// `scene.meshParts` order -- the same order GL's SceneRenderer::Build
    /// iterates, which GetBatches() callers (the oracle's report) depend
    /// on matching exactly. Calls Clear(ctx) first (mirrors GL's own
    /// Build(), which does the same), so calling Build() again on a
    /// live instance is safe and replaces the previous scene.
    ///
    /// `pipelines` supplies the descriptor-set layouts every batch's
    /// descriptor set is allocated against and the VkPipelineLayout later
    /// Render() calls push constants/descriptor sets through -- a pointer
    /// to it is cached (non-owning) for that later use, so `pipelines`
    /// must outlive this object until the next Build() or Clear(ctx).
    ///
    /// Returns false and fills err on any GPU resource failure (nothing
    /// is left half-built to Clear() manually -- Build() itself calls
    /// Clear(ctx) before returning false).
    bool Build(VkContext& ctx, const ScenePipelines& pipelines, const Parsers::SceneData& scene,
               std::string& err);

    /// Records the scene's draws onto `cmd` (which must already be inside
    /// an active dynamic-rendering pass, e.g. between a target's
    /// BeginFrame/EndFrame): sky batches first (if any -- the M0 corpus
    /// never sets isSky, so this path is wired but unexercised by
    /// task 5's smoke test), then opaque, then additive/blended, mirroring
    /// GL's RenderSky-then-Render pass order and RenderBatches' per-batch
    /// blend-pipeline selection (SceneRenderer.cpp:654-658 ported exactly:
    /// binary Additive-vs-not, Subtractive/EnvMap fall through to the
    /// Normal-blend pipeline). Sets the viewport/scissor to
    /// (viewportW, viewportH) via dynamic state before drawing anything.
    /// A no-op if Build() was never called or built zero batches.
    void Render(VkCommandBuffer cmd, const glm::mat4& view, const glm::mat4& proj,
                Rendering::ShadingMode mode, int viewportW, int viewportH);

    /// Draws the gradient background full-screen triangle through
    /// `pipeline` (T3's BackgroundPipeline). Unlike GL's `static`
    /// RenderBackground (which needs no instance state -- just a bound
    /// shader/VAO the global ShaderManager already owns), this Vulkan
    /// port lazily creates and caches its own small descriptor pool/UBO
    /// on first call (ctx is needed for that, and there is no implicit
    /// global Vulkan context to reach one through) and reuses them on
    /// every later call -- this is why the method is non-static and takes
    /// `ctx`. Independent of Build()/GetBatches(): may be called on an
    /// instance that has not Build() a scene yet, matching GL's own call
    /// order (the oracle draws the background before constructing that
    /// frame's SceneRenderer). Torn down by Clear(ctx) along with
    /// everything else this object owns.
    bool RenderBackground(VkContext& ctx, const BackgroundPipeline& pipeline, VkCommandBuffer cmd,
                          const glm::vec3& topColor, const glm::vec3& bottomColor, std::string& err);

    /// Draws the world-space grid through `pipeline` (T3's GridPipeline).
    /// The actual grid line / LOD-fade / axis-tint math already lives in
    /// grid.frag (a line-for-line port of GL's GRID_FRAG, done by T3 -- see
    /// that shader's top comment), so this method's job is exactly what
    /// GL's GridRenderer::Draw (Source/Rendering/GridRenderer.cpp) does on
    /// the CPU side: compute viewProj/invViewProj and feed them, plus
    /// gridColor/cameraPos/gridScale, into GridUBO before issuing the
    /// fullscreen-triangle draw (no vertex buffer, matching
    /// GridRenderer::Initialize's empty-VAO GL setup). cameraPos is derived
    /// the same way Render() derives SceneFrameUBO::cameraPos -- see
    /// Pipelines.h's "Camera convention" note. Lazily creates its own small
    /// descriptor pool/UBO on first call, same pattern as RenderBackground;
    /// torn down by Clear(ctx). Independent of Build()/GetBatches(), same
    /// as RenderBackground.
    ///
    /// AxisGizmo (Source/Rendering/AxisGizmo.cpp) is NOT ported here or
    /// anywhere in RenderVk: it draws entirely through ImGui's own
    /// ImDrawList (screen-space discs, hit-tested against
    /// ImGui::GetIO().MousePos) and never issues a GL or Vulkan call, so it
    /// already works unmodified against a Vulkan-rendered frame the same
    /// way it works against a GL one -- there is nothing backend-specific
    /// to port.
    bool RenderGrid(VkContext& ctx, const GridPipeline& pipeline, VkCommandBuffer cmd, const glm::mat4& view,
                    const glm::mat4& proj, const glm::vec4& gridColor, float gridScale, int viewportW,
                    int viewportH, std::string& err);

    /// Draws the skeleton/gizmo debug overlay through `pipeline` (T3's
    /// OverlayPipeline), porting GL's SceneRenderer::RenderSkeleton line
    /// generation exactly (Source/Rendering/SceneRenderer.cpp's
    /// RenderSkeleton): per joint, a parent-to-child bone line (or a
    /// 6-point root cross when the joint has no parent), a small
    /// joint-position dot cross, and 3 RGB orientation-axis line pairs --
    /// built from the same m_jointWorldPos Build() already computed via
    /// the shared Rendering::ComputeJointPalette (JointPalette.h/.cpp),
    /// baked through m_instanceTransform exactly like GL bakes it into
    /// every point before pushing it to its own line buffer. Colors use
    /// GL's own hardcoded fallback constants (SceneRenderer.cpp's
    /// `cfg ? ... : glm::vec4(...)` branch) unconditionally -- RenderVk has
    /// no dependency on Onyx::Services::AppConfig (its Get() definition
    /// lives in Onyx_Shell, which this target deliberately never links; see
    /// Tools/OnyxOracle/AppConfigStub.cpp's comment for the same
    /// constraint applied to onyx-oracle), and every context this milestone
    /// runs in (the oracle, onyx_tests) has no AppConfig instance anyway,
    /// so GL's own cfg-null fallback is the only value that could ever be
    /// observed here. A no-op (returns true without touching `cmd`) if
    /// Build() built no skeleton, matching GL's own early-return.
    ///
    /// The overlay vertex buffer is sized once in Build() (joints.size() *
    /// 16, an exact upper bound on this method's own per-joint vertex
    /// count -- see the .cpp for the derivation) and only memcpy'd into
    /// here, never (re)allocated mid-frame -- `cmd` may already be
    /// recording when this is called, and Resources::Upload's staged
    /// upload path is not legal to invoke against a command buffer that is
    /// itself still being recorded.
    bool RenderSkeleton(VkContext& ctx, const OverlayPipeline& pipeline, VkCommandBuffer cmd,
                        const glm::mat4& view, const glm::mat4& proj, int viewportW, int viewportH,
                        std::string& err);

    /// Destroys every GPU resource this object owns (batches, textures,
    /// frame UBOs, descriptor pools, the background helper's pool/UBO) and
    /// resets to a fresh, Build()-able state. Idempotent -- safe to call
    /// on a default-constructed or already-cleared instance.
    void Clear(VkContext& ctx);

    /// Same field set / same order as GL's SceneRenderer::GetBatches() --
    /// see the RenderBatch-reuse note at the top of this file. The oracle
    /// report (Tools/OnyxOracle/RenderReport.cpp) reads this directly.
    std::vector<Rendering::RenderBatch>& GetBatches() { return m_batches; }

private:
    /// The real Vulkan-side GPU resources for one batch -- index-aligned
    /// with m_batches (m_gpuBatches[i] belongs to m_batches[i]). Kept
    /// separate from Rendering::RenderBatch (whose GLuint fields are
    /// sentinels only, per the top-of-file note) rather than folded into
    /// it, so the reused bookkeeping struct never has to grow a
    /// Vulkan-shaped field GL callers of the same struct don't expect.
    struct GpuBatch {
        Buffer vertexBuf;
        Buffer indexBuf;
        uint32_t indexCount = 0;
        Buffer materialUbo;
        Buffer jointSsbo;
        VkDescriptorSet set = VK_NULL_HANDLE; // set 1, from m_descriptorPool
    };

    bool BuildBatch(VkContext& ctx, const ScenePipelines& pipelines, const Parsers::SceneData& scene,
                     const Parsers::MeshPart& part, std::string& err);
    void DrawBatch(VkCommandBuffer cmd, size_t index) const;

    std::vector<Rendering::RenderBatch> m_batches;
    std::vector<GpuBatch>               m_gpuBatches; // index-aligned with m_batches

    // Bucket membership, computed once in Build() -- mirrors GL's
    // m_opaqueBatches/m_additiveBatches/m_skyBatches (SceneRenderer.h),
    // just as indices into m_batches/m_gpuBatches instead of raw pointers
    // (m_batches is not touched again after Build() returns, so either
    // would be stable, but indices sidestep the question entirely).
    std::vector<size_t> m_opaqueIdx;
    std::vector<size_t> m_additiveIdx;
    std::vector<size_t> m_skyIdx;

    glm::mat4               m_instanceTransform{1.0f};

    // Idle-pose (rest, no animation) joint palette -- built by Build() via
    // the shared Rendering::ComputeJointPalette (JointPalette.h/.cpp; T6
    // deleted this file's own duplicate math and pointed it at that shared
    // code, per the M4 plan's mandate). m_skeleton/m_jointWorldPos exist
    // only for RenderSkeleton's debug-line generation below; T5's API had
    // no SetAnimation/UpdateAnimation equivalent, so the rest pose is what
    // every skinned corpus scene renders (rest pose != bind pose for
    // skinned-cube/joint-chain-200 by design, which is what actually
    // exercises the palette).
    std::vector<glm::mat4>               m_jointPalette;   // global joint index -> skinning matrix
    std::shared_ptr<Parsers::ObjectData> m_skeleton;       // kept for RenderSkeleton only
    std::vector<glm::vec3>               m_jointWorldPos;  // world-space joint origins, RenderSkeleton only

    const ScenePipelines* m_pipelines = nullptr; // non-owning, see Build()'s doc comment

    // Texture pool, index-aligned with scene.textures (Build() uploads
    // once, same contract as GL's SceneRenderer::Build). A shared 1x1
    // white default + one shared sampler back every one of a batch's 6
    // role slots that has no bound texture -- required because Vulkan
    // (without VK_EXT_descriptor_indexing's partiallyBound feature, which
    // this milestone does not enable) needs every element of a bound
    // sampler array to name a valid image, even the elements a shader
    // branch dynamically never samples.
    std::vector<Image2D> m_textures;
    Image2D               m_defaultTex;
    VkSampler              m_sampler = VK_NULL_HANDLE;

    // set 0 (per-frame) -- two persistent sets (main view + rotation-only
    // sky view), each backed by its own host-visible, persistently-mapped
    // SceneFrameUBO buffer so Render() can just memcpy new contents in
    // every call without touching the descriptor itself. Two distinct
    // buffers (not one buffer rewritten between the sky and main draws)
    // because both writes happen on the CPU before ANY of the frame's
    // commands are recorded, let alone submitted -- reusing one buffer for
    // both would let the second CPU write silently clobber the first
    // before the GPU ever reads either.
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE; // frame(2) + one set per batch
    Buffer           m_frameBufMain;
    Buffer           m_frameBufSky;
    void*            m_frameBufMainMapped = nullptr;
    void*            m_frameBufSkyMapped  = nullptr;
    VkDescriptorSet  m_frameSetMain = VK_NULL_HANDLE;
    VkDescriptorSet  m_frameSetSky  = VK_NULL_HANDLE;

    // RenderBackground's own small, lazily-created resources -- see that
    // method's doc comment for why they are separate from the pool above.
    VkDescriptorPool m_bgPool = VK_NULL_HANDLE;
    Buffer           m_bgBuf;
    void*            m_bgBufMapped = nullptr;
    VkDescriptorSet  m_bgSet = VK_NULL_HANDLE;

    // RenderGrid's own small, lazily-created resources -- same pattern as
    // RenderBackground's above (a GridUBO instead of a BackgroundUBO).
    VkDescriptorPool m_gridPool = VK_NULL_HANDLE;
    Buffer           m_gridBuf;
    void*            m_gridBufMapped = nullptr;
    VkDescriptorSet  m_gridSet = VK_NULL_HANDLE;

    // RenderSkeleton's resources: a lazily-created descriptor pool/UBO/set
    // (OverlayUBO, same pattern as RenderGrid/RenderBackground above) plus
    // a CPU-writable vertex buffer sized once in Build() -- see
    // RenderSkeleton's doc comment for why it cannot be (re)allocated
    // inside the method itself.
    VkDescriptorPool m_overlayPool = VK_NULL_HANDLE;
    Buffer           m_overlayBuf;
    void*            m_overlayBufMapped = nullptr;
    VkDescriptorSet  m_overlaySet = VK_NULL_HANDLE;
    Buffer           m_overlayVbo;
    void*            m_overlayVboMapped = nullptr;
    size_t           m_overlayVboCapacity = 0; // in OverlayVertex entries, not bytes
};

} // namespace Onyx::RenderVk
