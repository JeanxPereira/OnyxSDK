#pragma once

// ── Onyx::Exchange::ExportSceneData — glTF 2.0 export of Parsers::SceneData
// (T5, M5; spec §9) ─────────────────────────────────────────────────────
//
// Exporters mirror decoders: domain data OUT to an interchange format,
// instead of container bytes IN to domain data. glTF is v1's one format,
// and the choice is deliberate (spec §9's own words, worth repeating
// here): "glTF opened in Blender is an *external* oracle for skinning,
// weights and tangents — validation not written by us, independent of our
// renderer." Every render-correctness claim this SDK has made so far was
// checked against artifacts this SDK itself produced (the Vulkan/GL parity
// gate, the oracle corpus PNGs). This is the first check by something we
// did not write, and it only has teeth if the exported file is faithful —
// see the "no shortcuts" notes below.
//
// ── What gets exported ───────────────────────────────────────────────────
// Per Parsers::MeshPart -> one glTF primitive (triangle list): POSITION,
// NORMAL, TEXCOORD_0, TANGENT (Onyx::Domain::GpuVertex always carries a
// tangent, identity-default when a mesh ships none — see MeshVertex.h),
// plus JOINTS_0/WEIGHTS_0 when the part is skinned (see "Skins" below).
// Every Parsers::MeshPart in SceneData::meshParts becomes one primitive of
// a single glTF mesh — "1 mesh, N primitives", not N meshes.
//
// Per Parsers::MaterialDesc -> one glTF material: baseColorFactor
// (MaterialDesc::baseColor) and metallicFactor (MaterialDesc::metallic)
// always; roughnessFactor is left at glTF's own default (1.0) since
// MaterialDesc carries no roughness scalar to translate.
//
// ── TextureRole -> glTF texture slot mapping ────────────────────────────
// Of the nine Onyx::Parsers::TextureRole values, exactly three translate
// onto glTF 2.0's CORE material model with no repacking or extension:
//
//   Diffuse   -> pbrMetallicRoughness.baseColorTexture  (RGB + optional A)
//   Normal    -> material.normalTexture                 (tangent-space RGB)
//   Occlusion -> material.occlusionTexture               (R channel, AO)
//
// These three, plus baseColorFactor/metallicFactor above, are exactly the
// "materials with baseColor" surface this exporter promises. The other six
// roles are NOT exported, for two different reasons:
//
//   Height, Scatter, Detail, EnvMap
//       No glTF 2.0 CORE material slot exists for any of these at all —
//       displacement/height maps, subsurface-scattering maps, secondary
//       detail-texture blending, and a *per-material* environment map are
//       all either KHR extensions this exporter does not implement, or
//       (EnvMap) a scene-level IBL concept in glTF, not a per-material
//       texture slot in the first place. There is nothing "clean" to map
//       them onto without inventing a non-standard extension a Blender
//       import would ignore anyway.
//
//   Gloss, Emissive
//       Both have a plausible glTF neighbour (gloss -> the G/B channels of
//       metallicRoughnessTexture; emissive -> the native emissiveTexture
//       slot) but neither is a direct copy: our Gloss role is a raw
//       single-channel glossiness map, not pre-packed into glTF's
//       roughness=G/metallic=B channel convention, so exporting it as-is
//       under metallicRoughnessTexture would silently misrender (wrong
//       channel, wrong sense — glossiness is roughness's inverse) rather
//       than just "not appear". Emissive genuinely could be wired to
//       emissiveTexture with zero repacking, but is left out of this v1
//       pass deliberately to keep the exported role surface exactly the
//       set this task's brief named (Diffuse/Normal/Occlusion) rather than
//       silently growing it — a candidate for a follow-up, not a gap this
//       header hides.
//
// Every exported texture is re-encoded to PNG (stb_image_write, in
// memory) and embedded in the glTF binary buffer as an image bufferView —
// never left as an external file reference, so `out` alone is always a
// complete, self-contained asset regardless of `embedBuffers`.
//
// ── Skins — the point of this task ──────────────────────────────────────
// When `SceneData::HasSkeleton()` and `GltfOptions::includeSkin`:
//
//   - One glTF node per Parsers::Joint, parented per `Joint::parent`
//     (glTF's node.children, built from the same tree). Each node's
//     rest-pose translation/rotation/scale come from
//     ObjectData::vectors4/5/6 (idle local translation / Q.14 rotation /
//     local scale) — the SAME fields, and the SAME Euler-vs-quaternion
//     derivation, that Source/Rendering/JointPalette.cpp's BuildLocalTRS
//     uses to build the world rest pose the renderer skins with. See
//     Source/Exchange/GltfExport.cpp's top comment for exactly how that
//     derivation is reproduced here (Onyx_Exchange cannot link
//     Onyx_Render — see this header's "Why Core-only" note below — so it
//     cannot call BuildLocalTRS directly; the rotation math is mirrored
//     verbatim instead of re-derived from scratch, precisely so this
//     exporter and the renderer can never quietly disagree about what
//     "rest pose" means).
//   - glTF's inverseBindMatrices accessor is populated directly from
//     `Joint::bindToJointMat` — no re-derivation at all. That field IS
//     already the inverse bind pose (world -> joint local), the exact
//     matrix ComputeJointPalette() multiplies the world rest pose against
//     to get its per-joint skinning palette. Copying it verbatim is what
//     guarantees this exporter's skin and the renderer's skin are
//     mathematically the same skin, not two independent implementations
//     that happen to agree on the corpus by luck.
//   - Per-vertex JOINTS_0/WEIGHTS_0 come from GpuVertex::boneIndices/
//     boneWeights, remapped from MeshPart::jointMap's LOCAL indices
//     (0..jointMap.size()-1, per Rendering::BuildBatchPalette's own doc
//     comment) to the skin's joints array — which lists every skeleton
//     joint in ObjectData::joints order, so jointMap[local] is already the
//     correct glTF joint index with no further translation.
//
// A part with no skeleton, or with `MeshPart::useBindToJoint == false` or
// an empty `jointMap`, exports as an ordinary unskinned primitive (no
// JOINTS_0/WEIGHTS_0, mesh node carries no `skin`).
//
// ── Why Core-only, never Onyx::Render ───────────────────────────────────
// Onyx_Render already links Onyx_Core PUBLIC (root CMakeLists.txt); if
// Onyx_Exchange linked Onyx_Render too, and anything ever needed to go the
// other way, that would be a real CMake link cycle, not just a layering
// smell — the identical reason Include/Onyx/Cli/Render.h's CmdRender lives
// in Examples/OnyxCli/Render.cpp instead of Source/Cli/Commands.cpp. `decode
// --to gltf` (Include/Onyx/Cli/Gltf.h) follows that exact precedent: the
// CLI-level wiring in Source/Cli/Commands.cpp accepts an injected export
// hook rather than calling Onyx::Exchange directly, so Onyx_Core itself
// never links Onyx_Exchange either.
//
// ── Output shape ─────────────────────────────────────────────────────────
// `GltfOptions::embedBuffers == true` (default): writes a single self-
// contained .glb (binary glTF — JSON chunk + BIN chunk, produced by
// cgltf_write_file with cgltf_file_type_glb; the BIN chunk holds vertex/
// index/skin data AND every embedded PNG). `out`'s extension is not
// inspected — the file at `out` is always the format the options ask for.
// `GltfOptions::embedBuffers == false`: writes `out` as a plain-text
// .gltf JSON document, plus a sibling `<out stem>.bin` holding the exact
// same binary blob a .glb would have embedded (buffer.uri names that
// sibling file by its plain filename, so the pair must stay next to each
// other).

#include <Onyx/Parsers/SceneNode.h>

#include <filesystem>
#include <string>

namespace Onyx::Exchange {

struct GltfOptions {
    bool embedBuffers = true;  // true: single .glb. false: .gltf + sibling .bin
    bool includeSkin  = true;  // false: export geometry/materials only, no skin/joints
};

// Exports `scene` to `out` per `options`. Returns false and fills `err`
// with a human-readable reason (empty scene, unwritable `out`, ...) on
// failure — never throws. `scene.meshParts` must be non-empty; an empty
// scene is a caller error (ExportSceneData refuses it rather than writing
// a zero-primitive glTF file that would only fail confusingly later, in
// Blender or in a validator).
bool ExportSceneData(const Parsers::SceneData& scene, const std::filesystem::path& out,
                     const GltfOptions& options, std::string& err);

} // namespace Onyx::Exchange
