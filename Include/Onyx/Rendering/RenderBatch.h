#pragma once
#include <Onyx/Parsers/SceneNode.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════
// Task 11 (M4 "delete the GL renderer"): this header is the neutral home
// ShadingMode/RenderBatch/ResolveRoleIndices moved to when their previous
// owners -- Include/Onyx/Rendering/ShaderManager.h and
// Include/Onyx/Rendering/SceneRenderer.h -- were deleted along with the
// rest of the GL renderer. Both symbols were ALREADY reused by
// Onyx::Rendering::SceneRendererVk (see that class's own former "RenderBatch
// reuse" comment) and by Tools/OnyxOracle/RenderReport.h/CorpusScenes.h --
// deleting their old owners outright would have broken those live
// consumers, so the shared pieces move here first rather than die with the
// GL-only code around them (RenderBatch's rendering methods, GL texture
// upload, Shader/ShaderManager) that has no Vulkan equivalent and truly is
// GL-only.
//
// Kept exactly GL-free, same as its two previous owners: the texture-slot
// fields below are plain `uint32_t` handles, never `glad/glad.h`. `GpuMesh`
// is forward-declared only -- it was the GL SceneRenderer's own geometry
// object (deleted alongside it, Source/Rendering/GpuMesh.*) and
// RenderBatch::gpuMesh is a bookkeeping field no surviving code ever
// constructs or dereferences (SceneRendererVk's own Vulkan geometry lives
// in its private GpuBatch, not here) -- see the field's own comment.

namespace Onyx::Rendering {

class GpuMesh; // never defined after Task 11 -- see this header's top comment

/// Viewport shading mode. Was declared in the now-deleted ShaderManager.h.
///
/// Matcap/Wireframe/TexturedWire have NO Vulkan implementation in this
/// renderer -- SceneRendererVk has no code path for any of the three
/// (Matcap was alias-only on the GL renderer even before Task 11 deleted
/// it; Wireframe/TexturedWire never got a Vulkan port). The T11 fix round
/// removed all three from the shading-mode UI (combo box and both toolbar
/// cycle sites) so the picker never offers a choice with no visible
/// effect, but the enumerators themselves stay in this public API --
/// deleting them would be a breaking change to consumers pinned against
/// this header for no rendering benefit, since Onyx::Rendering has no
/// concept of "this enum value is UI-reachable." A caller that constructs
/// one of these three directly (bypassing the UI) gets undefined shading
/// behavior from SceneRendererVk, not a compile error or a runtime
/// diagnostic -- see CHANGELOG.md's "Known gaps" for the UI-side account
/// of this split.
enum class ShadingMode {
    Solid,            // Blinn-Phong with textures + lighting
    Matcap,           // Unimplemented in this (Vulkan) renderer -- see enum comment above
    Textured,         // Material preview (game textures, minimal lighting)
    Wireframe,        // Unimplemented in this (Vulkan) renderer -- see enum comment above
    TexturedWire      // Unimplemented in this (Vulkan) renderer -- see enum comment above
};

/// A single renderable batch: one mesh part + its resolved material.
/// Was declared in the now-deleted SceneRenderer.h; renderer-agnostic by
/// construction (see the field comments below) so both the retired GL
/// SceneRenderer and Onyx::Rendering::SceneRendererVk fill the exact same
/// shape -- GetBatches() order and field values are what Tools/OnyxOracle's
/// byte-stable report pins across both renderers.
struct RenderBatch {
    std::string                 name;
    bool                        isVisible = true;
    bool                        isHighlighted = false;

    // Never constructed nor dereferenced by any surviving renderer -- a
    // pure bookkeeping leftover from the GL SceneRenderer's own geometry
    // object. Stays null-initialized always; see this header's top comment.
    std::shared_ptr<GpuMesh>    gpuMesh;
    uint32_t                    texture0 = 0;       // Diffuse texture
    uint32_t                    texture1 = 0;       // Environment map / layer 1
    // PBR maps, in the texture layer order a role-suffixed texture naming
    // convention assigns them (a loader recognizes a texture's role -- normal,
    // ambient occlusion, gloss, subsurface scatter -- from a suffix on its
    // name). Zero means the material ships none, and the shader falls back to
    // a constant.
    uint32_t                    texNormal  = 0;     // layer 1 -- normal map
    uint32_t                    texAO      = 0;     // layer 2 -- ambient occlusion
    uint32_t                    texGloss   = 0;     // layer 3 -- gloss
    uint32_t                    texScatter = 0;     // layer 5 -- subsurface scatter
    float                       metallic   = 0.0f;
    float                       materialColor[4] = {1,1,1,1};
    float                       layerColor[4]    = {1,1,1,1};
    float                       uvOffset[2]      = {0,0};
    Parsers::BlendMode          blendMode = Parsers::BlendMode::Normal;
    uint32_t                    textureLayer = 0;
    std::vector<uint16_t>       jointMap;
    bool                        hasTexture = false;
    bool                        hasEnvmap  = false;
    bool                        hasSkeleton = false;
    bool                        isSky = false;
    uint64_t                    meshHash = 0;       // container-specific LOD-blob id (0 = internal/embedded)
    int                         vertexCount = 0;    // cached for inspector
    int                         triangleCount = 0;
};

/// Resolve a MaterialDesc's sparse role -> texture-pool-index map onto a
/// fixed array in TextureRole enum order (-1 = role absent). Pure -- no GL
/// calls, no Vulkan calls -- unit-testable without a GPU context. Was a
/// static Onyx::Rendering::SceneRenderer method; the definition now lives
/// in Source/Rendering/RenderBatch.cpp, a link-complete part of the
/// surviving Render layer (closes the Task-11 "link-completeness" defect:
/// Onyx::Rendering::SceneRendererVk::Build calls this, and previously the
/// only definition lived in the now-deleted GL-only SceneRenderer.cpp).
std::array<int, 9> ResolveRoleIndices(const Parsers::MaterialDesc& mat);

} // namespace Onyx::Rendering
