#pragma once

// See VkContext.h for the binding include-order rule (volk.h, then
// vk_mem_alloc.h, before any other Vulkan-touching header). VkResources.h
// / Pipelines.h already pull both in (via VkContext.h) in that order, so
// including them first here keeps the rule honored without repeating it.
#include <Onyx/RenderVk/Pipelines.h>
#include <Onyx/RenderVk/VkResources.h>

// ═══════════════════════════════════════════════════════════════════════
// RenderBatch reuse (task-5 brief's "bookkeeping question"): Tools/
// OnyxOracle/RenderReport.{h,cpp} ALREADY takes std::vector<Rendering::
// RenderBatch> directly and its own top comment states the reasoning this
// file leans on: SceneRenderer.h only forward-declares `GLuint`/`GLenum`
// as `using GLuint = unsigned int;` -- it does NOT include glad/GLFW, so
// pulling it in here does not violate Onyx_RenderVk's "no GL calls/headers"
// rule (verified: no GL header, no GL function call, just a plain integer
// typedef reused as a bookkeeping field). Including it also does NOT
// create a link dependency on Onyx_Render: RenderBatch's only non-trivial
// member is `std::shared_ptr<GpuMesh>`, which SceneRendererVk never
// constructs (it stays null-initialized; Vulkan geometry lives in this
// file's own GpuBatch, not in a GL GpuMesh), so GpuMesh.cpp (which lives
// in Onyx_Render and DOES touch real GL calls) is never pulled into this
// target's link step.
//
// Given that, the minimal move is to reuse Rendering::RenderBatch
// verbatim rather than extract a neutral struct: BuildReport()'s output
// shape is pinned to RenderBatch's exact field set already, so extracting
// a lookalike struct would only add a translation step (and a second
// place these fields could drift out of sync) for zero benefit -- the
// oracle's JSON report for a given scene comes out byte-identical whether
// GL's SceneRenderer or this file populated the vector, which is exactly
// the parity property task 5 exists to establish.
//
// The `GLuint texture0/texture1/texNormal/texAO/texGloss/texScatter`
// fields are populated here as plain nonzero/zero SENTINELS (never a real
// GPU handle of any kind, GL or Vulkan) -- their only consumers are
// RenderReport's CountBoundRoleTextures (nonzero => bound) and this
// struct's own hasTexture/hasEnvmap booleans, so a sentinel preserves
// the report's semantics exactly without smuggling a GL concept into a
// Vulkan translation unit. The real Vulkan-side texture/sampler state
// (VkImageView + VkSampler, wired into each batch's descriptor set) lives
// in this file's private GpuBatch, never in RenderBatch.
// ═══════════════════════════════════════════════════════════════════════
#include <Onyx/Rendering/SceneRenderer.h> // Rendering::RenderBatch, Rendering::ShadingMode

#include <Onyx/Parsers/SceneNode.h>

#include <glm/glm.hpp>

#include <cstdint>
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

    // Idle-pose joint palette -- a faithful port of GL's
    // SceneRenderer::ComputeJointPalette / BuildLocalTRS (Source/
    // Rendering/SceneRenderer.cpp), no animation (T5's API has no
    // SetAnimation/UpdateAnimation equivalent; the rest pose is what every
    // skinned corpus scene needs to render correctly even so, since rest
    // pose != bind pose for skinned-cube/joint-chain-200 by design).
    void ComputeJointPalette(const Parsers::ObjectData& skeleton);
    std::vector<glm::mat4> BuildBatchPalette(const std::vector<uint16_t>& jointMap) const;

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
    std::vector<glm::mat4>  m_jointPalette; // global joint index -> skinning matrix

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
};

} // namespace Onyx::RenderVk
