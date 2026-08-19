#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <map>
#include <memory>
#include <string>
#include <cstdint>
#include <Onyx/Parsers/MeshData.h>
#include <Onyx/Parsers/ObjectData.h>
#include <Onyx/Parsers/TextureData.h>
#include <Onyx/Parsers/AnimationData.h>

namespace Onyx::Parsers {


/// Blend mode for a render batch, parsed from mat.go flags
enum class BlendMode : uint8_t {
    Normal,       // Standard alpha: SRC_ALPHA, ONE_MINUS_SRC_ALPHA
    Additive,     // Glow/fire:      SRC_ALPHA, ONE
    Subtractive,  // Shadows
    EnvMap        // Environment map blend (strange blended in Go)
};

/// Explicit texture role a material can bind (spec §8.1). Replaces the old
/// positional layer index — a role identifies WHAT a texture is for, not
/// WHERE it happened to land in a per-game layer ordering.
enum class TextureRole : uint8_t {
    Diffuse, Normal, Occlusion, Gloss, Height, Scatter, Detail, Emissive, EnvMap
};

/// Material resolved from the WAD, ready to render. Does NOT own the GL
/// texture — that's created by the renderer.
struct MaterialDesc {
    float       baseColor[4]  = {1, 1, 1, 1};      // from mat header
    float       blendColor[4] = {1, 1, 1, 1};      // tint
    BlendMode   blendMode     = BlendMode::Normal;
    float       uvOffset[2]   = {0, 0};            // UV scroll offset
    float       metallic      = 0.0f;              // PBR metallic factor
    // role -> index into SceneData::textures; absent role = no map.
    std::map<TextureRole, int> textures;
};

/// Complete render-ready scene built from Instance → Object → Model chain.
/// One of these is produced per viewport/viewer. Owns all the data.
struct SceneData {
    // Transform from Instance (position/rotation/scale in world)
    glm::mat4                                   instanceTransform {1.0f};

    // If true, apply scale(1,1,-1) on top of instanceTransform.
    // GOW2 models face -Z and need this; GOWR is already in screen-correct space.
    bool                                        flipZ = true;

    // If true, this scene is a sky dome (rendered with rotation-only view, depth cleared after)
    bool                                        isSky = false;

    // Skeleton from Object (empty for static meshes)
    std::shared_ptr<ObjectData>                 skeleton;

    // Animation clips from ANM child (shared to allow player to reference)
    std::shared_ptr<AnimationData>              animations;

    // Geometry from Mesh (multiple parts, each with materialId + jointMap).
    // GOW2 per-part appearance variants become per-variant MaterialDescs at
    // load time; MeshPart::textureLayer survives only as a blend-ordering
    // hint (see the additive-batch heuristic in SceneRenderer::Build) — it no
    // longer indexes into a material's texture layers.
    std::vector<MeshPart>                       meshParts;

    // Materials from Model's child Material nodes (index = materialId)
    std::vector<MaterialDesc>                   materials;

    // Decoded textures ready for GPU upload — a FLAT pool. A MaterialDesc
    // resolves its per-role texture by indexing into this vector.
    std::vector<std::unique_ptr<TextureData>>   textures;

    bool HasSkeleton() const {
        return skeleton && skeleton->HasSkeleton();
    }

    bool IsEmpty() const {
        return meshParts.empty();
    }
};

} // namespace Onyx::Parsers
