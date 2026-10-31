#pragma once

#include <Onyx/Core/Domain/Scene.h>
#include <Onyx/Core/Result.h>

#include <filesystem>

namespace Onyx::Exchange {

struct GltfOptions {
    // true writes one self-contained .glb; false writes a .gltf plus a sibling <stem>.bin.
    bool embedBuffers = true;
    bool includeSkin = true;
    bool includeAnimations = true;
};

// One mesh with one primitive per non-empty MeshPart. Attributes follow what the part carries: POSITION, NORMAL,
// TANGENT, TEXCOORD_0/1, COLOR_0, and JOINTS_0/WEIGHTS_0 when the scene has a skeleton and the part's jointMap is
// not empty (the part's joint indices are mapped through jointMap to skeleton joints). Indices are u32.
// One material per Domain::Material: baseColorFactor, metallicFactor, and the Diffuse, Normal and Occlusion textures
// (re-encoded as PNG inside the buffer; a texture that is not Rgba8 leaves its slot unset). One node per Joint with
// its localBind decomposed to translation, rotation and scale, inverse bind matrices from Joint::inverseBind, and one
// animation per non-additive clip with LINEAR samplers. POSITION and the other float accessors carry min and max; the
// index and JOINTS_0 accessors carry none (a float bound cannot hold an index above 2^24 exactly).
// A `.gltf` output writes `<stem>.bin` beside it and replaces any file of that name.
// Errors: exchange.gltf.empty (no part has vertices), exchange.gltf.write (encoding or the filesystem refused).
Result<void> WriteGltf(const Domain::Scene& scene, const std::filesystem::path& out, const GltfOptions& options = {});

}
