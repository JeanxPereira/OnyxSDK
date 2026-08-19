#pragma once

// See VkContext.h for the binding include-order rule (volk.h, then
// vk_mem_alloc.h, before any other Vulkan-touching header). VkContext.h
// already pulls both in, in that order, so including it first here keeps
// the rule honored without repeating it.
#include <Onyx/RenderVk/VkContext.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace Onyx::RenderVk {

// ═══════════════════════════════════════════════════════════════════════
// Camera convention (binding for every task that builds a view/projection
// matrix for this renderer, starting with T5's SceneRendererVk/Camera
// port) — the one place the plan's fixed rule
// ("right-handed GLM with GLM_FORCE_DEPTH_ZERO_TO_ONE and a Y-flip
// handled in the projection, NOT negative viewport") is documented, per
// the plan's Global Constraints section.
//
// `GLM_FORCE_DEPTH_ZERO_TO_ONE` is defined PRIVATE on the Onyx_RenderVk
// CMake target (root CMakeLists.txt) -- every Onyx_RenderVk source that
// includes a <glm/gtc/...> projection header gets Vulkan's [0,1] clip
// depth instead of GL's [-1,1] automatically. PRIVATE, not PUBLIC: this
// milestone's composition roots (onyx-oracle, onyx_tests) link BOTH
// Onyx::Render (GL) and Onyx::RenderVk in the same executable, and a
// PUBLIC definition here was caught leaking into Tools/OnyxOracle/
// CorpusScenes.cpp's GL-path glm::perspective() calls, corrupting the
// frozen GL golden corpus (OracleMatchesGolden) -- see the CMakeLists.txt
// comment at Onyx_RenderVk's target_compile_definitions for the full
// story. Any future Vulkan camera code MUST live inside Onyx_RenderVk's
// own sources (e.g. Source/RenderVk/SceneRendererVk.cpp, T5) to inherit
// this define; code outside that target will not see it and must not
// try to. This is also why Source/RenderVk/Shaders/grid.frag had to
// change its NDC-Z handling (see that file's divergence-3 comment) while
// scene.vert/scene.frag did not need to (neither reads/writes clip-space
// Z directly).
//
// The Y-flip: Vulkan's NDC Y points down where GL's points up. Whoever
// builds the projection matrix (glm::perspective or equivalent) MUST
// negate its [1][1] element afterward:
//     glm::mat4 proj = glm::perspective(fovy, aspect, near, far);
//     proj[1][1] *= -1.0f;
// NOT by flipping the viewport (VkViewport.height negative + y offset) —
// that alternate trick works too but is explicitly NOT the convention
// this milestone uses, so mixing the two must never happen. No shader in
// Source/RenderVk/Shaders performs a Y-flip itself; gl_Position is always
// `projection * view * worldPos` unchanged from the GL source.
// ═══════════════════════════════════════════════════════════════════════

// Fixed dynamic-rendering attachment formats for the whole milestone
// (dynamic rendering needs format info at pipeline-creation time, not
// draw time — VkPipelineRenderingCreateInfo below). Matches VkResources.h
// AspectMaskFor's comment: D32_SFLOAT is the plan's fixed depth format.
inline constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
inline constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

// Fixed MSAA sample count. Not asked for verbatim by this task's brief
// (which names only the formats), but required here regardless: Vulkan
// requires a graphics pipeline's rasterizationSamples to match the actual
// color/depth attachments it draws into, and T4's OffscreenTarget is
// already specified (plan, Task 4) as "4x MSAA + resolve" — matching the
// GL viewport's own MSAA framebuffer (Source/Viewers/Viewport3D.cpp's
// m_msaaFbo). Building these pipelines single-sampled would make them
// unusable the moment T4 lands, so the sample count is fixed here too.
inline constexpr VkSampleCountFlagBits kSampleCount = VK_SAMPLE_COUNT_4_BIT;

// ── Descriptor scheme (fixed here; consumed by T4-T8) ───────────────────
// set 0 = per-frame; set 1 = per-batch (scene pipelines only — grid and
// background each get their own much smaller set 0, see below). Push
// constant = per-draw model matrix (scene pipelines only), 64 bytes,
// vertex stage.

/// set 0, binding 0 for the scene pipelines — mirrors scene.vert/
/// scene.frag's `FrameUBO` (std140). uShadingMode keeps the GL integer
/// values (0 = Solid, 2 = Textured); 1 (Matcap) is never sent — see
/// scene.frag's divergence-2 comment for why Matcap has no Vulkan path.
struct alignas(16) SceneFrameUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 cameraPos;
    int32_t   shadingMode = 0;
};
// std140: two mat4 (64B each) + vec3 packed with the trailing int into one
// 16B slot (a scalar immediately after a vec3 is a legal std140 pack — the
// scalar's own 4B alignment divides the vec3's 12B offset evenly). Static
// asserts here catch any accidental struct drift from the GLSL mirror
// (scene.vert/scene.frag's FrameUBO) at compile time rather than at a
// device-validation layer months from now.
static_assert(sizeof(SceneFrameUBO) == 144, "SceneFrameUBO must match scene.vert/frag's std140 FrameUBO");

/// set 1, binding 0 for the scene pipelines — mirrors scene.vert/
/// scene.frag's `MaterialUBO` (std140). `flags` is a bitmask of
/// SceneFlags::k*.
struct alignas(16) SceneMaterialUBO {
    glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 layerColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2 uvOffset{0.0f, 0.0f};
    float     metallic = 0.0f;
    uint32_t  flags = 0;
};
static_assert(sizeof(SceneMaterialUBO) == 48, "SceneMaterialUBO must match scene.vert/frag's std140 MaterialUBO");

/// SceneMaterialUBO::flags bits (scene.vert/scene.frag: FLAG_* consts).
/// Bits 0-4 mirror GL's per-batch texture-presence uniforms
/// (uUseTexture/uHasNormal/uHasAO/uHasGloss/uHasScatter); bit 5 mirrors
/// GL's uUseJoints (moved here from a standalone uniform — see
/// scene.frag's divergence-7 comment).
namespace SceneFlags {
inline constexpr uint32_t kUseTexture = 1u << 0;
inline constexpr uint32_t kHasNormal  = 1u << 1;
inline constexpr uint32_t kHasAO      = 1u << 2;
inline constexpr uint32_t kHasGloss   = 1u << 3;
inline constexpr uint32_t kHasScatter = 1u << 4;
inline constexpr uint32_t kUseJoints  = 1u << 5;
} // namespace SceneFlags

/// set 1, binding 1 combined-sampler slot indices (scene.frag: ROLE_*
/// consts) — a straight reindex of the loader's own material-role layer
/// order (Source/Rendering/ShaderManager.cpp's SCENE_FRAG comment: role 0
/// diffuse, role 1 normal `_0n_`, role 2 AO `_0ao_`, role 3 gloss `_0g_`,
/// role 5 scatter `_0sc_`). Index 4 is reserved/unused: no consumer reads
/// it in scene.frag, but every batch still binds all 6 slots (an absent
/// role binds the shared default per the brief's descriptor scheme) so
/// the array stays uniformly sized.
namespace SceneRole {
inline constexpr int kDiffuse = 0;
inline constexpr int kNormal  = 1;
inline constexpr int kAO      = 2;
inline constexpr int kGloss   = 3;
// index 4 reserved/unused
inline constexpr int kScatter = 5;
inline constexpr int kCount   = 6;
} // namespace SceneRole

/// set 0, binding 0 for the grid pipeline — mirrors grid.frag's
/// `GridUBO` (std140).
struct alignas(16) GridUBO {
    glm::mat4 viewProj;
    glm::mat4 invViewProj;
    glm::vec4 gridColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 cameraPos;
    float     gridScale = 1.0f;
};
static_assert(sizeof(GridUBO) == 160, "GridUBO must match grid.frag's std140 GridUBO");

/// set 0, binding 0 for the background pipeline — mirrors
/// background.frag's `BackgroundUBO` (std140). Only .rgb of each is read
/// (background.frag); the alpha lane exists so this stays a plain vec4
/// pair on both sides of the boundary.
struct alignas(16) BackgroundUBO {
    glm::vec4 topColor{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 bottomColor{0.0f, 0.0f, 0.0f, 1.0f};
};
static_assert(sizeof(BackgroundUBO) == 32, "BackgroundUBO must match background.frag's std140 BackgroundUBO");

/// Everything a scene draw needs: descriptor set layouts, pipeline
/// layout, and the fixed-blend-state pipeline variants dynamic rendering
/// requires at creation time (no per-draw blend toggle exists in Vulkan
/// 1.3 core without a pipeline switch). Owned by the caller (T5's
/// SceneRendererVk); Create/Destroy bracket a VkContext's lifetime the
/// same way VkResources' Buffer/Image2D do.
///
/// The three pipelines mirror the three blend behaviors
/// SceneRenderer::Render/RenderBatches (GL) actually produces today
/// (Source/Rendering/SceneRenderer.cpp:546-718): opaque batches
/// (depth test LESS, write on, blend off); then translucent batches
/// (depth test LEQUAL — GL's comment: "Required for coplanar layered
/// geometry", write off) split by per-batch blend func into normal alpha
/// (SRC_ALPHA/ONE_MINUS_SRC_ALPHA — everything not Additive, including
/// Subtractive/EnvMap batches, which GL does not yet give a distinct
/// blend func) and additive (SRC_ALPHA/ONE). No wireframe-overlay
/// pipeline exists (scene.frag's divergence-4: that debug pass is out of
/// scope for this task).
struct ScenePipelines {
    VkDescriptorSetLayout frameSetLayout = VK_NULL_HANDLE; // set 0
    VkDescriptorSetLayout batchSetLayout = VK_NULL_HANDLE; // set 1
    VkPipelineLayout      layout         = VK_NULL_HANDLE;
    VkPipeline            opaque         = VK_NULL_HANDLE;
    VkPipeline            blendNormal    = VK_NULL_HANDLE;
    VkPipeline            blendAdditive  = VK_NULL_HANDLE;
};

/// Reused for both the grid overlay AND skeleton-line debug drawing: GL's
/// SceneRenderer::RenderSkeleton draws through the "grid" GL program
/// rather than a dedicated shader (confirmed by reading
/// Source/Rendering/SceneRenderer.cpp:722-812), so no overlay.vert/frag
/// pair or overlay pipeline exists here — see grid.frag's top comment.
struct GridPipeline {
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE; // set 0
    VkPipelineLayout      layout    = VK_NULL_HANDLE;
    VkPipeline            pipeline  = VK_NULL_HANDLE;
};

struct BackgroundPipeline {
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE; // set 0
    VkPipelineLayout      layout    = VK_NULL_HANDLE;
    VkPipeline            pipeline  = VK_NULL_HANDLE;
};

/// Stateless pipeline-object factory, mirroring VkResources::Resources'
/// shape: every method takes the VkContext it operates on. Pipeline
/// objects are plain structs the caller owns; each must be passed to the
/// matching Destroy() exactly once.
class Pipelines {
public:
    Pipelines() = delete;

    /// Creates the scene descriptor layouts, pipeline layout (with the
    /// 64-byte push constant range), and all three blend-state pipeline
    /// variants, targeting kColorFormat/kDepthFormat via dynamic
    /// rendering (no VkRenderPass). Returns a default (layout ==
    /// VK_NULL_HANDLE) ScenePipelines and fills err on failure.
    static bool CreateScene(VkContext& ctx, ScenePipelines& out, std::string& err);
    static void Destroy(VkContext& ctx, ScenePipelines& p);

    /// Creates the grid pipeline: no vertex input (the fullscreen
    /// triangle is generated from gl_VertexIndex, matching
    /// GridRenderer::Initialize's empty-VAO GL setup), depth test on
    /// (LESS_OR_EQUAL, matching worldPos vs. real geometry), write off,
    /// alpha blend on (SRC_ALPHA/ONE_MINUS_SRC_ALPHA).
    static bool CreateGrid(VkContext& ctx, GridPipeline& out, std::string& err);
    static void Destroy(VkContext& ctx, GridPipeline& p);

    /// Creates the background pipeline: no vertex input, depth test AND
    /// write both off (matches SceneRenderer::RenderBackground's
    /// glDisable(GL_DEPTH_TEST) + glDepthMask(GL_FALSE)), blend off.
    static bool CreateBackground(VkContext& ctx, BackgroundPipeline& out, std::string& err);
    static void Destroy(VkContext& ctx, BackgroundPipeline& p);
};

} // namespace Onyx::RenderVk
