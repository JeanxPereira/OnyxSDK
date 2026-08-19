#include <Onyx/Exchange/GltfExport.h>

// ── GltfExport.cpp — the one glTF exporter, and the one place this SDK
// re-derives rest-pose/skinning math outside Source/Rendering/
// JointPalette.cpp ──────────────────────────────────────────────────────
//
// Onyx_Exchange links Onyx_Core only (root CMakeLists.txt) — it cannot
// link Onyx_Render, because Onyx_Render already links Onyx_Core PUBLIC,
// and the reverse link would be a real CMake cycle (see Include/Onyx/
// Exchange/GltfExport.h's "Why Core-only" note, and Include/Onyx/Cli/
// Render.h's fully-written-out precedent for the identical constraint on
// the `render` CLI command). That means this file cannot call
// Onyx::Rendering::BuildLocalTRS/ComputeJointPalette
// (Source/Rendering/JointPalette.cpp) directly, even though it needs the
// exact same two things that file computes:
//
//   1. Each joint's rest-pose local rotation, derived from
//      ObjectData::vectors5 (Q.14-encoded Euler or quaternion, selected by
//      Joint::isQuaternion) — see JointLocalRotation() below, a verbatim
//      copy of BuildLocalTRS's rotation half. Translation and scale need
//      no re-derivation at all: they ARE ObjectData::vectors4/vectors6,
//      the same raw fields BuildLocalTRS reads for its T and S matrices.
//   2. Each joint's inverse bind matrix — NOT re-derived here either.
//      ObjectData::Joint::bindToJointMat already IS that matrix (world ->
//      joint local); ComputeJointPalette multiplies the world rest pose
//      against it directly. This file copies it into the glTF
//      inverseBindMatrices accessor byte-for-byte.
//
// The rotation formula is duplicated rather than shared through a common
// header because JointPalette.h/.cpp live in the Render layer (compiled
// into Onyx_Render, CMakeLists.txt's ONYX_RENDER_SOURCES) and pull in
// nothing else Render-specific — but moving them to Core would be a
// bigger, unrequested refactor of a file three other renderer call sites
// already depend on. Duplication here is small (one ~25-line function),
// isolated, and explicitly cited against its source of truth so a future
// change to BuildLocalTRS's math is at least discoverable by grepping for
// this comment. If the two ever disagree, this exporter's glTF and the
// Vulkan renderer's viewport will visibly disagree too — which is exactly
// what spec §9 wants glTF-in-Blender to be able to catch, just aimed at
// the wrong target (our own bug, not a format question) if it happens.
//
// ── Binary layout ─────────────────────────────────────────────────────
// Every accessor (per mesh part: POSITION/NORMAL/TEXCOORD_0/TANGENT/
// indices, plus JOINTS_0/WEIGHTS_0 when skinned; once per export:
// inverseBindMatrices) gets its own non-interleaved bufferView, 4-byte
// aligned, all inside ONE cgltf_buffer. Every exported texture (PNG-
// encoded in memory via stb_image_write) is appended to the SAME buffer
// as its own bufferView + cgltf_image, so the output is self-contained
// regardless of GltfOptions::embedBuffers: that flag only decides whether
// this one blob ends up as a .glb BIN chunk (cgltf_write_file embeds
// data->bin/data->bin_size automatically for cgltf_file_type_glb) or a
// sibling .bin file this code writes itself (cgltf_write_file, per its
// own doc comment, never writes buffer/image bytes for a plain .gltf).
//
// ── cgltf_data construction ──────────────────────────────────────────
// cgltf_write's CGLTF_WRITE_IDXPROP/IDXARRPROP macros compute every JSON
// index via POINTER SUBTRACTION against the arrays living directly in
// cgltf_data (e.g. `node->mesh - context->data->meshes`). That means every
// std::vector backing one of those arrays (accessors/bufferViews/buffers/
// materials/images/textures/samplers/nodes/meshes/skins/scenes, plus the
// per-primitive attribute arrays and per-node children arrays) is sized to
// its EXACT final count up front and filled by index — never push_back'd
// past that size — so no pointer taken into it is ever invalidated by a
// later reallocation.

#include <Onyx/Domain/MeshVertex.h>
#include <Onyx/Parsers/MeshData.h>
#include <Onyx/Parsers/ObjectData.h>
#include <Onyx/Parsers/TextureData.h>

#define CGLTF_WRITE_IMPLEMENTATION
#include <cgltf_write.h>

// STB_IMAGE_WRITE_STATIC gives every stb_image_write symbol internal
// linkage in this one TU (STBIWDEF -> `static`, see stb_image_write.h's
// own comment on the macro). Without it, this file's `#define
// STB_IMAGE_WRITE_IMPLEMENTATION` would externally define
// stbi_write_png_to_mem et al a SECOND time in any binary that also links
// Examples/OnyxCli/Render.cpp (which vendors its own externally-linked
// copy for the exact same reason — see that file's own ODR comment) or
// Tools/OnyxOracle/PngWrite.cpp: onyxbox-cli links both Render.cpp
// directly AND Onyx::Exchange (for `decode --to gltf`, Include/Onyx/Cli/
// Gltf.h), so without STATIC linkage that combination would be a genuine
// duplicate-symbol link error, not just a style nit.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace Onyx::Exchange {

using Onyx::Domain::GpuVertex;
using Onyx::Parsers::BlendMode;
using Onyx::Parsers::MaterialDesc;
using Onyx::Parsers::MeshPart;
using Onyx::Parsers::ObjectData;
using Onyx::Parsers::SceneData;
using Onyx::Parsers::TextureData;
using Onyx::Parsers::TextureRole;

namespace {

char* MutableCStr(const char* s) { return const_cast<char*>(s); }

// Growable blob every accessor/image is appended into, 4-byte aligned
// (the strictest alignment any component type here needs, so every
// bufferView offset satisfies every validator's "vertex attribute
// bufferViews are 4-byte aligned" rule, not just the accessor spec's own
// component-size minimum).
struct BinBlob {
    std::vector<uint8_t> bytes;
    size_t Append(const void* data, size_t size) {
        size_t pad = (4 - (bytes.size() % 4)) % 4;
        bytes.insert(bytes.end(), pad, uint8_t(0));
        size_t offset = bytes.size();
        const uint8_t* p = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), p, p + size);
        return offset;
    }
};

// Verbatim mirror of the rotation half of Source/Rendering/
// JointPalette.cpp's BuildLocalTRS -- see this file's top comment for why
// it is reproduced here instead of called. Reads exactly what
// BuildLocalTRS reads for rotation (obj.vectors5[i], obj.joints[i].
// isQuaternion) and nothing else; translation/scale need no equivalent
// function since they ARE obj.vectors4[i]/obj.vectors6[i] directly.
glm::quat JointLocalRotation(const ObjectData& obj, size_t i) {
    if (i >= obj.vectors5.size() || i >= obj.joints.size()) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const auto& v5 = obj.vectors5[i];
    const float Q14 = 1.0f / (1 << 14);
    const bool isQuat = obj.joints[i].isQuaternion;

    float qx, qy, qz, qw;
    if (isQuat) {
        qx = float(v5.x) * Q14;
        qy = float(v5.y) * Q14;
        qz = float(v5.z) * Q14;
        qw = float(v5.w) * Q14;
    } else {
        const float halfToRad = (0.5f * glm::pi<float>()) / 180.0f;
        float ex = float(v5.x) * Q14 * 360.0f * halfToRad;
        float ey = float(v5.y) * Q14 * 360.0f * halfToRad;
        float ez = float(v5.z) * Q14 * 360.0f * halfToRad;
        float sx = std::sin(ex), cx = std::cos(ex);
        float sy = std::sin(ey), cy = std::cos(ey);
        float sz = std::sin(ez), cz = std::cos(ez);
        qx = sx * cy * cz - cx * sy * sz;
        qy = cx * sy * cz + sx * cy * sz;
        qz = cx * cy * sz - sx * sy * cz;
        qw = cx * cy * cz + sx * sy * sz;
    }
    float qlen = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (qlen > 0.0001f) { qx /= qlen; qy /= qlen; qz /= qlen; qw /= qlen; }
    else                 { qx = 0; qy = 0; qz = 0; qw = 1; }
    return glm::quat(qw, qx, qy, qz);
}

void ComputeMinMax(const float* flat, size_t count, int dim, float* mn, float* mx) {
    for (int k = 0; k < dim; ++k) { mn[k] = 0.0f; mx[k] = 0.0f; }
    if (count == 0) return;
    for (int k = 0; k < dim; ++k) { mn[k] = flat[k]; mx[k] = flat[k]; }
    for (size_t i = 1; i < count; ++i) {
        for (int k = 0; k < dim; ++k) {
            float v = flat[i * size_t(dim) + size_t(k)];
            if (v < mn[k]) mn[k] = v;
            if (v > mx[k]) mx[k] = v;
        }
    }
}

} // namespace

bool ExportSceneData(const SceneData& scene, const std::filesystem::path& out,
                     const GltfOptions& options, std::string& err) {
    if (scene.meshParts.empty()) {
        err = "SceneData has no mesh parts to export";
        return false;
    }

    const bool hasSkin = options.includeSkin && scene.HasSkeleton();
    const ObjectData* skel = hasSkin ? scene.skeleton.get() : nullptr;
    const size_t jointCount = skel ? skel->joints.size() : 0;

    // Matches Source/RenderVk/SceneRendererVk.cpp's own skinning gate
    // exactly (`batch.hasSkeleton && !batch.jointMap.empty()`, no
    // useBindToJoint check) -- the renderer decides what is skinned, this
    // exporter must describe what the renderer draws, not apply its own
    // policy on top. useBindToJoint deliberately does NOT gate this: a
    // part can carry a non-empty jointMap with useBindToJoint == false
    // (MeshData.h's isRigid comment: "GOWR: submesh has no BoneIdx/BoneWgt
    // semantics; rigid-bound to jointMap[0]") and the renderer still fully
    // skins it via that single-entry jointMap. Gating on useBindToJoint
    // too would silently export such a part as static bind-pose geometry
    // while the viewport renders it skinned -- exactly the kind of
    // exporter/renderer disagreement spec §9 exists to catch, on the wrong
    // side, with no fixture noticing.
    std::vector<bool> partSkinned(scene.meshParts.size(), false);
    for (size_t i = 0; i < scene.meshParts.size(); ++i) {
        const MeshPart& p = scene.meshParts[i];
        partSkinned[i] = hasSkin && !p.jointMap.empty();
    }

    // Texture roles that map cleanly onto glTF core PBR (see GltfExport.h
    // for the full role-by-role writeup of what does NOT map and why).
    // Dedupe by scene texture pool index, first-seen order.
    std::vector<int> usedTexIdx;
    std::map<int, int> texToSlot;
    auto UseTex = [&](int idx) {
        if (idx < 0 || size_t(idx) >= scene.textures.size() || !scene.textures[size_t(idx)]) return;
        if (texToSlot.count(idx)) return;
        texToSlot[idx] = int(usedTexIdx.size());
        usedTexIdx.push_back(idx);
    };
    for (const auto& mat : scene.materials) {
        for (TextureRole role : {TextureRole::Diffuse, TextureRole::Normal, TextureRole::Occlusion}) {
            auto it = mat.textures.find(role);
            if (it != mat.textures.end()) UseTex(it->second);
        }
    }

    // ── counts (fixed before any vector is allocated) ────────────────────
    size_t accessorCount = 0;
    for (size_t i = 0; i < scene.meshParts.size(); ++i) {
        accessorCount += 5; // POSITION, NORMAL, TEXCOORD_0, TANGENT, indices
        if (partSkinned[i]) accessorCount += 2; // JOINTS_0, WEIGHTS_0
    }
    if (hasSkin) accessorCount += 1; // inverseBindMatrices

    const size_t imageCount = usedTexIdx.size();
    const size_t bufferViewCount = accessorCount + imageCount;
    const size_t materialCount = scene.materials.size();
    const size_t textureCount = imageCount;
    const size_t samplerCount = imageCount > 0 ? 1 : 0;
    const size_t nodeCount = 1 + jointCount; // mesh node + one node per joint
    const size_t skinCount = hasSkin ? 1 : 0;

    std::vector<cgltf_accessor> accessors(accessorCount);
    std::vector<cgltf_buffer_view> bufferViews(bufferViewCount);
    std::vector<cgltf_buffer> buffers(1);
    std::vector<cgltf_material> materials(materialCount);
    std::vector<cgltf_image> images(imageCount);
    std::vector<cgltf_texture> textures(textureCount);
    std::vector<cgltf_sampler> samplers(samplerCount);
    std::vector<cgltf_node> nodes(nodeCount);
    std::vector<cgltf_mesh> meshes(1);
    std::vector<cgltf_skin> skins(skinCount);
    std::vector<cgltf_scene> scenes(1);
    std::vector<cgltf_primitive> primitives(scene.meshParts.size());
    std::vector<std::vector<cgltf_attribute>> attrStorage(scene.meshParts.size());
    std::vector<std::vector<cgltf_node*>> childStorage(jointCount);

    BinBlob blob;
    size_t accessorCursor = 0;
    size_t bufferViewCursor = 0;

    auto AddAccessor = [&](cgltf_type type, cgltf_component_type ctype, size_t count,
                            const void* data, size_t elemByteSize,
                            const float* minv, const float* maxv, int dim) -> cgltf_accessor* {
        size_t byteLen = elemByteSize * count;
        size_t bufOffset = blob.Append(data, byteLen);

        cgltf_buffer_view& bv = bufferViews[bufferViewCursor++];
        bv.buffer = &buffers[0];
        bv.offset = bufOffset;
        bv.size = byteLen;

        cgltf_accessor& acc = accessors[accessorCursor++];
        acc.component_type = ctype;
        acc.type = type;
        acc.count = count;
        acc.buffer_view = &bv;
        acc.has_min = true;
        acc.has_max = true;
        std::memcpy(acc.min, minv, sizeof(float) * size_t(dim));
        std::memcpy(acc.max, maxv, sizeof(float) * size_t(dim));
        return &acc;
    };

    // ── per-part geometry ─────────────────────────────────────────────────
    for (size_t pi = 0; pi < scene.meshParts.size(); ++pi) {
        const MeshPart& part = scene.meshParts[pi];
        const size_t vcount = part.vertices.size();

        std::vector<float> pos(vcount * 3), nrm(vcount * 3), uv(vcount * 2), tan(vcount * 4);
        for (size_t v = 0; v < vcount; ++v) {
            const GpuVertex& gv = part.vertices[v];
            pos[v * 3 + 0] = gv.position.x; pos[v * 3 + 1] = gv.position.y; pos[v * 3 + 2] = gv.position.z;
            nrm[v * 3 + 0] = gv.normal.x;   nrm[v * 3 + 1] = gv.normal.y;   nrm[v * 3 + 2] = gv.normal.z;
            uv[v * 2 + 0]  = gv.uv.x;       uv[v * 2 + 1]  = gv.uv.y;
            tan[v * 4 + 0] = gv.tangent.x;  tan[v * 4 + 1] = gv.tangent.y;
            tan[v * 4 + 2] = gv.tangent.z;  tan[v * 4 + 3] = gv.tangent.w;
        }

        float mn[16], mx[16];
        ComputeMinMax(pos.data(), vcount, 3, mn, mx);
        cgltf_accessor* posAcc = AddAccessor(cgltf_type_vec3, cgltf_component_type_r_32f, vcount,
                                              pos.data(), sizeof(float) * 3, mn, mx, 3);
        ComputeMinMax(nrm.data(), vcount, 3, mn, mx);
        cgltf_accessor* nrmAcc = AddAccessor(cgltf_type_vec3, cgltf_component_type_r_32f, vcount,
                                              nrm.data(), sizeof(float) * 3, mn, mx, 3);
        ComputeMinMax(uv.data(), vcount, 2, mn, mx);
        cgltf_accessor* uvAcc = AddAccessor(cgltf_type_vec2, cgltf_component_type_r_32f, vcount,
                                             uv.data(), sizeof(float) * 2, mn, mx, 2);
        ComputeMinMax(tan.data(), vcount, 4, mn, mx);
        cgltf_accessor* tanAcc = AddAccessor(cgltf_type_vec4, cgltf_component_type_r_32f, vcount,
                                              tan.data(), sizeof(float) * 4, mn, mx, 4);

        cgltf_accessor* jointsAcc = nullptr;
        cgltf_accessor* weightsAcc = nullptr;
        if (partSkinned[pi]) {
            std::vector<uint16_t> joints(vcount * 4, uint16_t(0));
            std::vector<float> weights(vcount * 4, 0.0f);
            for (size_t v = 0; v < vcount; ++v) {
                const GpuVertex& gv = part.vertices[v];
                const uint32_t local[4] = {gv.boneIndices.x, gv.boneIndices.y, gv.boneIndices.z,
                                            gv.boneIndices.w};
                for (int c = 0; c < 4; ++c) {
                    // MeshPart::jointMap maps LOCAL index -> global skeleton
                    // joint index (Rendering::BuildBatchPalette's own doc
                    // comment); the skin's joints array below lists every
                    // skeleton joint in ObjectData::joints order, so that
                    // global index IS already the correct glTF joint index.
                    uint16_t global = (local[c] < part.jointMap.size())
                                           ? part.jointMap[local[c]]
                                           : uint16_t(0);
                    joints[v * 4 + size_t(c)] = global;
                }
                weights[v * 4 + 0] = gv.boneWeights.x; weights[v * 4 + 1] = gv.boneWeights.y;
                weights[v * 4 + 2] = gv.boneWeights.z; weights[v * 4 + 3] = gv.boneWeights.w;
            }
            std::vector<float> jf(vcount * 4);
            for (size_t k = 0; k < jf.size(); ++k) jf[k] = float(joints[k]);
            ComputeMinMax(jf.data(), vcount, 4, mn, mx);
            jointsAcc = AddAccessor(cgltf_type_vec4, cgltf_component_type_r_16u, vcount,
                                     joints.data(), sizeof(uint16_t) * 4, mn, mx, 4);
            ComputeMinMax(weights.data(), vcount, 4, mn, mx);
            weightsAcc = AddAccessor(cgltf_type_vec4, cgltf_component_type_r_32f, vcount,
                                      weights.data(), sizeof(float) * 4, mn, mx, 4);
        }

        std::vector<float> idxF(part.indices.size());
        for (size_t k = 0; k < idxF.size(); ++k) idxF[k] = float(part.indices[k]);
        ComputeMinMax(idxF.data(), part.indices.size(), 1, mn, mx);
        cgltf_accessor* idxAcc = AddAccessor(cgltf_type_scalar, cgltf_component_type_r_32u,
                                              part.indices.size(), part.indices.data(),
                                              sizeof(uint32_t), mn, mx, 1);

        std::vector<cgltf_attribute>& attrs = attrStorage[pi];
        attrs.reserve(6);
        attrs.push_back(cgltf_attribute{MutableCStr("POSITION"), cgltf_attribute_type_position, 0, posAcc});
        attrs.push_back(cgltf_attribute{MutableCStr("NORMAL"), cgltf_attribute_type_normal, 0, nrmAcc});
        attrs.push_back(cgltf_attribute{MutableCStr("TEXCOORD_0"), cgltf_attribute_type_texcoord, 0, uvAcc});
        attrs.push_back(cgltf_attribute{MutableCStr("TANGENT"), cgltf_attribute_type_tangent, 0, tanAcc});
        if (partSkinned[pi]) {
            attrs.push_back(cgltf_attribute{MutableCStr("JOINTS_0"), cgltf_attribute_type_joints, 0, jointsAcc});
            attrs.push_back(cgltf_attribute{MutableCStr("WEIGHTS_0"), cgltf_attribute_type_weights, 0, weightsAcc});
        }

        cgltf_primitive& prim = primitives[pi];
        prim.type = cgltf_primitive_type_triangles;
        prim.indices = idxAcc;
        prim.material = (part.materialId < materials.size()) ? &materials[part.materialId] : nullptr;
        prim.attributes = attrs.data();
        prim.attributes_count = attrs.size();
    }

    // ── inverse bind matrices — copied verbatim, not re-derived ─────────
    cgltf_accessor* ibmAcc = nullptr;
    if (hasSkin) {
        std::vector<float> ibmFlat(jointCount * 16);
        for (size_t j = 0; j < jointCount; ++j) {
            std::memcpy(&ibmFlat[j * 16], glm::value_ptr(skel->joints[j].bindToJointMat), sizeof(float) * 16);
        }
        float mn[16], mx[16];
        ComputeMinMax(ibmFlat.data(), jointCount, 16, mn, mx);
        ibmAcc = AddAccessor(cgltf_type_mat4, cgltf_component_type_r_32f, jointCount,
                              ibmFlat.data(), sizeof(float) * 16, mn, mx, 16);
    }

    // ── embedded texture images (PNG, encoded in memory) ─────────────────
    if (imageCount > 0) {
        samplers[0].mag_filter = cgltf_filter_type_linear;
        samplers[0].min_filter = cgltf_filter_type_linear;
        samplers[0].wrap_s = cgltf_wrap_mode_repeat;
        samplers[0].wrap_t = cgltf_wrap_mode_repeat;
    }
    for (size_t k = 0; k < usedTexIdx.size(); ++k) {
        const TextureData& tex = *scene.textures[size_t(usedTexIdx[k])];
        int pngLen = 0;
        unsigned char* png = stbi_write_png_to_mem(
            tex.pixels.empty() ? nullptr : tex.pixels.data(),
            int(tex.width * 4), int(tex.width), int(tex.height), 4, &pngLen);
        if (!png) {
            err = "failed to PNG-encode texture '" + tex.name + "' for glTF embedding";
            return false;
        }
        size_t off = blob.Append(png, size_t(pngLen));
        std::free(png);

        cgltf_buffer_view& bv = bufferViews[bufferViewCursor++];
        bv.buffer = &buffers[0];
        bv.offset = off;
        bv.size = size_t(pngLen);

        images[k].buffer_view = &bv;
        images[k].mime_type = MutableCStr("image/png");
        textures[k].image = &images[k];
        textures[k].sampler = &samplers[0];
    }

    // ── materials ─────────────────────────────────────────────────────────
    for (size_t mi = 0; mi < materials.size(); ++mi) {
        const MaterialDesc& src = scene.materials[mi];
        cgltf_material& m = materials[mi];
        m.has_pbr_metallic_roughness = true;
        m.pbr_metallic_roughness.base_color_factor[0] = src.baseColor[0];
        m.pbr_metallic_roughness.base_color_factor[1] = src.baseColor[1];
        m.pbr_metallic_roughness.base_color_factor[2] = src.baseColor[2];
        m.pbr_metallic_roughness.base_color_factor[3] = src.baseColor[3];
        m.pbr_metallic_roughness.metallic_factor = src.metallic;
        m.pbr_metallic_roughness.roughness_factor = 1.0f; // MaterialDesc carries no roughness scalar

        auto BindTex = [&](TextureRole role, cgltf_texture_view& view) {
            auto it = src.textures.find(role);
            if (it == src.textures.end()) return;
            auto slot = texToSlot.find(it->second);
            if (slot == texToSlot.end()) return;
            view.texture = &textures[size_t(slot->second)];
            view.texcoord = 0;
            view.scale = 1.0f;
        };
        BindTex(TextureRole::Diffuse, m.pbr_metallic_roughness.base_color_texture);
        BindTex(TextureRole::Normal, m.normal_texture);
        BindTex(TextureRole::Occlusion, m.occlusion_texture);

        // glTF core has no Additive/Subtractive/EnvMap blend mode — BLEND
        // is the closest available approximation for anything that is not
        // plain opaque Normal blending; a known, documented fidelity loss
        // (GltfExport.h does not promise blend-mode fidelity, only the
        // geometry/material/skin surface it names explicitly).
        const bool translucent = src.baseColor[3] < 0.999f || src.blendMode != BlendMode::Normal;
        m.alpha_mode = translucent ? cgltf_alpha_mode_blend : cgltf_alpha_mode_opaque;
        m.double_sided = true; // safest default for a human eyeballing the export in Blender
    }

    // ── skeleton node hierarchy (rest pose) ──────────────────────────────
    if (hasSkin) {
        for (size_t j = 0; j < jointCount; ++j) {
            cgltf_node& jn = nodes[1 + j];
            jn.name = MutableCStr(skel->joints[j].name.c_str());

            glm::vec3 t(0.0f);
            if (j < skel->vectors4.size()) {
                const auto& v4 = skel->vectors4[j];
                t = glm::vec3(v4.x, v4.y, v4.z);
            }
            glm::vec3 s(1.0f);
            if (j < skel->vectors6.size()) {
                const auto& v6 = skel->vectors6[j];
                s = glm::vec3(v6.x != 0.0f ? v6.x : 1.0f, v6.y != 0.0f ? v6.y : 1.0f,
                              v6.z != 0.0f ? v6.z : 1.0f);
            }
            glm::quat q = JointLocalRotation(*skel, j);

            jn.has_translation = true;
            jn.translation[0] = t.x; jn.translation[1] = t.y; jn.translation[2] = t.z;
            jn.has_rotation = true;
            jn.rotation[0] = q.x; jn.rotation[1] = q.y; jn.rotation[2] = q.z; jn.rotation[3] = q.w;
            jn.has_scale = true;
            jn.scale[0] = s.x; jn.scale[1] = s.y; jn.scale[2] = s.z;
        }
        for (size_t j = 0; j < jointCount; ++j) {
            int16_t p = skel->joints[j].parent;
            if (p >= 0 && size_t(p) < jointCount) {
                childStorage[size_t(p)].push_back(&nodes[1 + j]);
            }
        }
        for (size_t j = 0; j < jointCount; ++j) {
            if (!childStorage[j].empty()) {
                nodes[1 + j].children = childStorage[j].data();
                nodes[1 + j].children_count = childStorage[j].size();
            }
        }
    }

    // ── mesh node — carries the exported geometry (+ skin, if any) ──────
    cgltf_node& meshNode = nodes[0];
    meshNode.name = MutableCStr("SceneMesh");
    meshNode.mesh = &meshes[0];
    // scene.flipZ mirrors what the renderer actually shows (SceneRendererVk
    // applies the same scale(1,1,-1) at draw time for GOW2-space meshes) --
    // baking it into the mesh node's own matrix keeps the exported glTF
    // visually consistent with the corpus PNG a human compares it against
    // in Blender (docs/gltf-validation.md), not just topologically correct.
    glm::mat4 root = scene.instanceTransform;
    if (scene.flipZ) root = root * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, -1.0f));
    meshNode.has_matrix = true;
    std::memcpy(meshNode.matrix, glm::value_ptr(root), sizeof(float) * 16);

    std::vector<cgltf_node*> jointPtrs;
    if (hasSkin) {
        jointPtrs.resize(jointCount);
        for (size_t j = 0; j < jointCount; ++j) jointPtrs[j] = &nodes[1 + j];

        int rootIdx = -1;
        int rootCount = 0;
        for (size_t j = 0; j < jointCount; ++j) {
            if (skel->joints[j].parent < 0) {
                ++rootCount;
                if (rootIdx < 0) rootIdx = int(j);
            }
        }
        cgltf_skin& sk = skins[0];
        sk.joints = jointPtrs.data();
        sk.joints_count = jointPtrs.size();
        sk.inverse_bind_matrices = ibmAcc;
        if (rootCount == 1) sk.skeleton = &nodes[1 + size_t(rootIdx)];
        meshNode.skin = &sk;
    }

    meshes[0].name = MutableCStr("SceneMesh");
    meshes[0].primitives = primitives.data();
    meshes[0].primitives_count = primitives.size();

    std::vector<cgltf_node*> sceneNodes;
    sceneNodes.push_back(&meshNode);
    if (hasSkin) {
        for (size_t j = 0; j < jointCount; ++j) {
            if (skel->joints[j].parent < 0) sceneNodes.push_back(&nodes[1 + j]);
        }
    }
    scenes[0].nodes = sceneNodes.data();
    scenes[0].nodes_count = sceneNodes.size();

    // ── assemble cgltf_data and write ────────────────────────────────────
    buffers[0].size = blob.bytes.size();

    std::string generator = "OnyxSDK Onyx::Exchange::ExportSceneData";
    cgltf_data data{};
    data.asset.version = MutableCStr("2.0");
    data.asset.generator = MutableCStr(generator.c_str());
    data.meshes = meshes.data();       data.meshes_count = meshes.size();
    data.materials = materialCount ? materials.data() : nullptr; data.materials_count = materialCount;
    data.accessors = accessors.data(); data.accessors_count = accessors.size();
    data.buffer_views = bufferViews.data(); data.buffer_views_count = bufferViews.size();
    data.buffers = buffers.data();     data.buffers_count = buffers.size();
    data.images = imageCount ? images.data() : nullptr;       data.images_count = imageCount;
    data.textures = textureCount ? textures.data() : nullptr; data.textures_count = textureCount;
    data.samplers = samplerCount ? samplers.data() : nullptr; data.samplers_count = samplerCount;
    data.skins = skinCount ? skins.data() : nullptr;          data.skins_count = skinCount;
    data.nodes = nodes.data();         data.nodes_count = nodes.size();
    data.scenes = scenes.data();       data.scenes_count = scenes.size();
    data.scene = &scenes[0];

    std::error_code ec;
    if (out.has_parent_path()) std::filesystem::create_directories(out.parent_path(), ec);

    cgltf_options wopts{};
    std::string binFileName;
    if (options.embedBuffers) {
        wopts.type = cgltf_file_type_glb;
        data.file_type = cgltf_file_type_glb;
        data.bin = blob.bytes.data();
        data.bin_size = blob.bytes.size();
    } else {
        wopts.type = cgltf_file_type_gltf;
        data.file_type = cgltf_file_type_gltf;
        binFileName = out.stem().string() + ".bin";
        buffers[0].uri = MutableCStr(binFileName.c_str());
    }

    cgltf_result res = cgltf_write_file(&wopts, out.string().c_str(), &data);
    if (res != cgltf_result_success) {
        err = "cgltf_write_file failed (code " + std::to_string(int(res)) + ") writing " + out.string();
        return false;
    }

    if (!options.embedBuffers) {
        std::filesystem::path binPath = out.has_parent_path() ? (out.parent_path() / binFileName)
                                                                : std::filesystem::path(binFileName);
        std::ofstream bf(binPath, std::ios::binary | std::ios::trunc);
        if (!bf) {
            err = "failed to open " + binPath.string() + " for writing";
            return false;
        }
        if (!blob.bytes.empty()) {
            bf.write(reinterpret_cast<const char*>(blob.bytes.data()), std::streamsize(blob.bytes.size()));
        }
        if (!bf) {
            err = "failed to write " + binPath.string();
            return false;
        }
    }

    return true;
}

} // namespace Onyx::Exchange
