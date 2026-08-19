#include "CorpusScenes.h"
#include "CorpusTextures.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Onyx::OracleTool {

using Onyx::Domain::GpuVertex;
using Onyx::Parsers::BlendMode;
using Onyx::Parsers::MaterialDesc;
using Onyx::Parsers::MeshPart;
using Onyx::Parsers::ObjectData;
using Onyx::Parsers::SceneData;
using Onyx::Parsers::TextureRole;

namespace {

// ── UV sphere ────────────────────────────────────────────────────────────
// Standard stacks x slices UV sphere. Pole rows degenerate to zero-area
// triangles (r == 0 at the poles) -- harmless and fully deterministic.
MeshPart BuildUvSphere(float radius, int stacks, int slices, glm::vec3 center,
                        std::string name) {
    MeshPart part;
    part.name = std::move(name);
    part.useBindToJoint = false; // static geometry, no skeleton

    const float kPi = glm::pi<float>();
    const int slicesV = slices + 1;

    for (int stack = 0; stack <= stacks; ++stack) {
        float v = float(stack) / float(stacks);
        float phi = v * kPi;
        float z = radius * std::cos(phi);
        float r = radius * std::sin(phi);
        for (int slice = 0; slice <= slices; ++slice) {
            float u = float(slice) / float(slices);
            float theta = u * 2.0f * kPi;
            float x = r * std::cos(theta);
            float y = r * std::sin(theta);

            GpuVertex vert{};
            vert.position = glm::vec3(x, y, z) + center;
            vert.normal = (radius > 0.0f) ? glm::vec3(x, y, z) / radius
                                           : glm::vec3(0.0f, 0.0f, 1.0f);
            vert.uv = glm::vec2(u, v);
            part.vertices.push_back(vert);
        }
    }

    for (int stack = 0; stack < stacks; ++stack) {
        for (int slice = 0; slice < slices; ++slice) {
            uint32_t i0 = uint32_t(stack * slicesV + slice);
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + uint32_t(slicesV);
            uint32_t i3 = i2 + 1;
            part.indices.insert(part.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }

    return part;
}

// ── Ringed box ───────────────────────────────────────────────────────────
// Shared by the two skinned scenes. Extrudes a halfX x halfY rectangular
// cross-section through the given (ascending) z-levels: one 4-vertex ring
// per level with radial side-wall normals, plus dedicated +-Z-normal cap
// vertices at the first and last level so the box reads as a closed solid.
// Positions are absolute (already in the bind-pose world frame the caller
// wants) -- callers pass world-space z-levels directly, no post-transform.
struct BoxGeometry {
    std::vector<GpuVertex> vertices;
    std::vector<uint32_t>  indices;
};

BoxGeometry BuildRingedBox(float halfX, float halfY, const std::vector<float>& zLevels) {
    BoxGeometry geo;
    const size_t nRings = zLevels.size();

    static const std::array<glm::vec2, 4> kCorner = {
        glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f),
        glm::vec2(1.0f, 1.0f),   glm::vec2(-1.0f, 1.0f)
    };

    // Side walls.
    for (size_t r = 0; r < nRings; ++r) {
        float t = (nRings > 1) ? float(r) / float(nRings - 1) : 0.0f;
        for (int c = 0; c < 4; ++c) {
            GpuVertex v{};
            v.position = glm::vec3(kCorner[c].x * halfX, kCorner[c].y * halfY, zLevels[r]);
            v.normal = glm::normalize(glm::vec3(kCorner[c].x, kCorner[c].y, 0.0f));
            v.uv = glm::vec2(float(c) / 4.0f, t);
            geo.vertices.push_back(v);
        }
    }
    for (size_t r = 0; r + 1 < nRings; ++r) {
        for (uint32_t c = 0; c < 4; ++c) {
            uint32_t c1 = (c + 1) % 4;
            uint32_t i0 = uint32_t(r * 4 + c);
            uint32_t i1 = uint32_t(r * 4 + c1);
            uint32_t i2 = uint32_t((r + 1) * 4 + c1);
            uint32_t i3 = uint32_t((r + 1) * 4 + c);
            geo.indices.insert(geo.indices.end(), {i0, i1, i2, i0, i2, i3});
        }
    }

    // Bottom cap (outward normal -Z).
    uint32_t bottomBase = uint32_t(geo.vertices.size());
    for (int c = 0; c < 4; ++c) {
        GpuVertex v{};
        v.position = glm::vec3(kCorner[c].x * halfX, kCorner[c].y * halfY, zLevels.front());
        v.normal = glm::vec3(0.0f, 0.0f, -1.0f);
        v.uv = glm::vec2(kCorner[c].x * 0.5f + 0.5f, kCorner[c].y * 0.5f + 0.5f);
        geo.vertices.push_back(v);
    }
    geo.indices.insert(geo.indices.end(), {
        bottomBase + 0, bottomBase + 2, bottomBase + 1,
        bottomBase + 0, bottomBase + 3, bottomBase + 2
    });

    // Top cap (outward normal +Z).
    uint32_t topBase = uint32_t(geo.vertices.size());
    for (int c = 0; c < 4; ++c) {
        GpuVertex v{};
        v.position = glm::vec3(kCorner[c].x * halfX, kCorner[c].y * halfY, zLevels.back());
        v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        v.uv = glm::vec2(kCorner[c].x * 0.5f + 0.5f, kCorner[c].y * 0.5f + 0.5f);
        geo.vertices.push_back(v);
    }
    geo.indices.insert(geo.indices.end(), {
        topBase + 0, topBase + 1, topBase + 2,
        topBase + 0, topBase + 2, topBase + 3
    });

    return geo;
}

// Q.14 fixed-point encoder for the Euler-degrees lane BuildLocalTRS (see
// Source/Rendering/SceneRenderer.cpp) decodes as `v5[i] * (1/16384) * 360`.
int EncodeEulerDegreesQ14(double degrees) {
    return int(std::lround(degrees / 360.0 * 16384.0));
}

} // namespace

// ── sphere-grid ──────────────────────────────────────────────────────────

CorpusScene BuildSphereGrid() {
    CorpusScene cs;
    cs.name = "sphere-grid";
    SceneData& scene = cs.scene;
    scene.flipZ = false; // synthetic scene is already in world-correct space

    // Flat 10-entry texture pool, in role-listing order from the spec.
    scene.textures.push_back(MakeChecker(64, {255, 0, 0, 255}, {255, 255, 255, 255}, "sphere_diffuse_checker"));      // 0 Diffuse
    scene.textures.push_back(MakeBumpNormal(64, "sphere_normal_bump"));                                              // 1 Normal
    scene.textures.push_back(MakeGradient(64, {255, 255, 255, 255}, {128, 128, 128, 255}, "sphere_occlusion_grad")); // 2 Occlusion
    scene.textures.push_back(MakeGradient(64, {0, 0, 0, 255}, {255, 255, 255, 255}, "sphere_gloss_grad"));           // 3 Gloss
    scene.textures.push_back(MakeSolid(64, {128, 128, 128, 255}, "sphere_height_solid"));                            // 4 Height
    scene.textures.push_back(MakeSolid(64, {200, 60, 60, 255}, "sphere_scatter_solid"));                             // 5 Scatter
    scene.textures.push_back(MakeChecker(32, {0, 0, 255, 255}, {255, 255, 255, 255}, "sphere_detail_checker"));      // 6 Detail
    scene.textures.push_back(MakeSolid(64, {0, 0, 0, 255}, "sphere_emissive_black"));                                // 7 Emissive rows 0-1
    scene.textures.push_back(MakeSolid(64, {0, 80, 0, 255}, "sphere_emissive_green"));                               // 8 Emissive row 2
    scene.textures.push_back(MakeGradient(64, {135, 206, 235, 255}, {255, 255, 255, 255}, "sphere_envmap_grad"));    // 9 EnvMap

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            float x = -2.0f + 2.0f * float(col);
            float y = -2.0f + 2.0f * float(row);

            // metallic in {0, 0.5, 1} by column.
            float metallic = float(col) * 0.5f;
            MeshPart part = BuildUvSphere(
                0.8f, 24, 32, glm::vec3(x, y, 0.0f),
                "sphere_c" + std::to_string(col) + "_r" + std::to_string(row) +
                    "_metallic" + std::to_string(metallic));
            part.materialId = uint32_t(row * 3 + col);
            scene.meshParts.push_back(std::move(part));

            MaterialDesc mat;
            mat.baseColor[0] = 0.4f + 0.3f * float(col);
            mat.baseColor[1] = 0.4f + 0.3f * float(row);
            mat.baseColor[2] = 0.6f;
            mat.baseColor[3] = 1.0f;
            mat.blendMode = BlendMode::Normal;
            mat.metallic = metallic;
            mat.textures[TextureRole::Diffuse] = 0;
            mat.textures[TextureRole::Normal] = 1;
            mat.textures[TextureRole::Occlusion] = 2;
            mat.textures[TextureRole::Gloss] = 3;
            mat.textures[TextureRole::Height] = 4;
            mat.textures[TextureRole::Scatter] = 5;
            mat.textures[TextureRole::Detail] = 6;
            mat.textures[TextureRole::Emissive] = (row == 2) ? 8 : 7;
            mat.textures[TextureRole::EnvMap] = 9;
            scene.materials.push_back(mat);
        }
    }

    cs.view = glm::lookAt(glm::vec3(0.0f, -7.0f, 2.5f), glm::vec3(0.0f, 0.0f, 0.0f),
                           glm::vec3(0.0f, 0.0f, 1.0f));
    cs.proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
    return cs;
}

// ── skinned-cube ─────────────────────────────────────────────────────────

CorpusScene BuildSkinnedCube() {
    CorpusScene cs;
    cs.name = "skinned-cube";
    SceneData& scene = cs.scene;
    scene.flipZ = false;

    scene.textures.push_back(MakeChecker(64, {255, 165, 0, 255}, {255, 255, 255, 255}, "cube_diffuse_checker"));

    MaterialDesc mat;
    mat.textures[TextureRole::Diffuse] = 0;
    scene.materials.push_back(mat);

    // ── Skeleton: 3-joint chain, bind pose straight, rest pose bent. ─────
    //
    // Fields populated below are exactly the ones
    // Rendering::SceneRenderer::ComputeJointPalette()'s no-animation path
    // reads (see Source/Rendering/SceneRenderer.cpp, BuildLocalTRS +
    // ComputeJointPalette): per joint, obj.joints[i].parent,
    // obj.joints[i].isQuaternion, obj.vectors4[i] (local translation),
    // obj.vectors5[i] (Q.14 Euler rotation), obj.vectors6[i] (local scale),
    // and obj.joints[i].bindToJointMat (inverse bind pose) -- combined as
    // palette[i] = worldRestPose[i] * bindToJointMat[i]. Matrixes1/2/3 are
    // NOT read by this path (the code comments explain Matrixes1 was
    // retired in favour of the Vectors4/5/6 TRS chain), so they are left
    // empty here on purpose.
    auto obj = std::make_shared<ObjectData>();
    constexpr int kJointCount = 3;
    const float bindZ[kJointCount] = {0.0f, 1.33f, 2.67f};

    obj->joints.resize(kJointCount);
    obj->vectors4.resize(kJointCount);
    obj->vectors5.resize(kJointCount);
    obj->vectors6.resize(kJointCount);

    for (int i = 0; i < kJointCount; ++i) {
        auto& j = obj->joints[i];
        j.id = int16_t(i);
        j.parent = (i == 0) ? int16_t(-1) : int16_t(i - 1);
        j.name = "joint" + std::to_string(i);
        j.isQuaternion = false; // Euler path, matches GOW2 convention
        j.isSkinned = true;

        float localZ = (i == 0) ? bindZ[0] : (bindZ[i] - bindZ[i - 1]);
        obj->vectors4[i] = glm::vec4(0.0f, 0.0f, localZ, 1.0f);
        obj->vectors6[i] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

        // Rest pose bends joints 1 and 2 by +30 degrees around X; joint 0
        // (the root) is unbent.
        int bendQ14 = (i == 1 || i == 2) ? EncodeEulerDegreesQ14(30.0) : 0;
        obj->vectors5[i] = glm::ivec4(bendQ14, 0, 0, 0);

        // Bind pose = straight chain (no rotation) -> bindToJointMat is the
        // inverse of that straight-chain world transform.
        glm::mat4 bindWorld = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, bindZ[i]));
        j.bindToJointMat = glm::inverse(bindWorld);
    }
    scene.skeleton = obj;

    // ── Mesh: one box, 8 rings spanning z=[0,4], skinned by ring distance
    // to the two nearest joints (bind-pose Z), normalized. ───────────────
    std::vector<float> zLevels;
    for (int i = 0; i < 8; ++i) {
        zLevels.push_back(4.0f * float(i) / 7.0f);
    }
    BoxGeometry geo = BuildRingedBox(0.5f, 0.5f, zLevels);

    MeshPart part;
    part.name = "cube_body";
    part.materialId = 0;
    part.jointMap = {0, 1, 2}; // local index == global joint index
    part.useBindToJoint = true;

    for (const auto& src : geo.vertices) {
        GpuVertex v = src;

        float d[kJointCount];
        for (int k = 0; k < kJointCount; ++k) {
            d[k] = std::fabs(v.position.z - bindZ[k]);
        }
        std::array<int, kJointCount> order = {0, 1, 2};
        std::sort(order.begin(), order.end(), [&](int p, int q) { return d[p] < d[q]; });
        int a = order[0];
        int b = order[1];
        float da = d[a];
        float db = d[b];
        float wa, wb;
        if (da + db > 1e-8f) {
            wa = db / (da + db);
            wb = da / (da + db);
        } else {
            wa = 1.0f;
            wb = 0.0f;
        }
        v.boneWeights = glm::vec4(wa, wb, 0.0f, 0.0f);
        v.boneIndices = glm::uvec4(uint32_t(a), uint32_t(b), 0u, 0u);
        part.vertices.push_back(v);
    }
    part.indices = geo.indices;
    scene.meshParts.push_back(std::move(part));

    cs.view = glm::lookAt(glm::vec3(4.0f, -6.0f, 2.0f), glm::vec3(0.0f, 0.0f, 1.5f),
                           glm::vec3(0.0f, 0.0f, 1.0f));
    cs.proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
    return cs;
}

// ── blend-stack ──────────────────────────────────────────────────────────

CorpusScene BuildBlendStack() {
    CorpusScene cs;
    cs.name = "blend-stack";
    SceneData& scene = cs.scene;
    scene.flipZ = false;

    scene.textures.push_back(MakeChecker(64, {128, 128, 128, 255}, {255, 255, 255, 255}, "floor_checker")); // 0
    scene.textures.push_back(MakeSolid(64, {255, 255, 255, 255}, "quad_normal_white"));                      // 1
    scene.textures.push_back(MakeSolid(64, {40, 40, 255, 255}, "quad_additive_blue"));                       // 2
    scene.textures.push_back(MakeSolid(64, {60, 60, 60, 255}, "quad_subtractive_grey"));                     // 3

    MaterialDesc floorMat;
    floorMat.blendMode = BlendMode::Normal;
    floorMat.textures[TextureRole::Diffuse] = 0;
    scene.materials.push_back(floorMat); // 0

    MaterialDesc normalMat;
    normalMat.blendMode = BlendMode::Normal;
    normalMat.baseColor[3] = 0.5f;
    normalMat.textures[TextureRole::Diffuse] = 1;
    scene.materials.push_back(normalMat); // 1

    MaterialDesc addMat;
    addMat.blendMode = BlendMode::Additive;
    addMat.textures[TextureRole::Diffuse] = 2;
    scene.materials.push_back(addMat); // 2

    MaterialDesc subMat;
    subMat.blendMode = BlendMode::Subtractive;
    subMat.textures[TextureRole::Diffuse] = 3;
    scene.materials.push_back(subMat); // 3

    auto makeQuad = [](float halfX, float halfY, float z, std::string name) {
        MeshPart part;
        part.name = std::move(name);
        part.useBindToJoint = false;

        static const std::array<glm::vec2, 4> kCorner = {
            glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f),
            glm::vec2(1.0f, 1.0f),   glm::vec2(-1.0f, 1.0f)
        };
        static const std::array<glm::vec2, 4> kUv = {
            glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f),
            glm::vec2(1.0f, 1.0f), glm::vec2(0.0f, 1.0f)
        };
        for (int i = 0; i < 4; ++i) {
            GpuVertex v{};
            v.position = glm::vec3(kCorner[i].x * halfX, kCorner[i].y * halfY, z);
            v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            v.uv = kUv[i];
            part.vertices.push_back(v);
        }
        part.indices = {0, 1, 2, 0, 2, 3};
        return part;
    };

    MeshPart floor = makeQuad(3.0f, 3.0f, 0.0f, "floor");
    floor.materialId = 0;
    scene.meshParts.push_back(std::move(floor));

    MeshPart qNormal = makeQuad(1.0f, 1.0f, 0.5f, "quad_normal");
    qNormal.materialId = 1;
    scene.meshParts.push_back(std::move(qNormal));

    MeshPart qAdd = makeQuad(1.0f, 1.0f, 1.0f, "quad_additive");
    qAdd.materialId = 2;
    scene.meshParts.push_back(std::move(qAdd));

    MeshPart qSub = makeQuad(1.0f, 1.0f, 1.5f, "quad_subtractive");
    qSub.materialId = 3;
    scene.meshParts.push_back(std::move(qSub));

    cs.view = glm::lookAt(glm::vec3(0.0f, -6.0f, 4.0f), glm::vec3(0.0f, 0.0f, 0.8f),
                           glm::vec3(0.0f, 0.0f, 1.0f));
    cs.proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
    return cs;
}

// ── joint-chain-200 ──────────────────────────────────────────────────────

CorpusScene BuildJointChain200() {
    CorpusScene cs;
    cs.name = "joint-chain-200";
    SceneData& scene = cs.scene;
    scene.flipZ = false;

    scene.textures.push_back(MakeSolid(64, {200, 200, 60, 255}, "chain_diffuse_solid"));
    MaterialDesc mat;
    mat.textures[TextureRole::Diffuse] = 0;
    scene.materials.push_back(mat);

    constexpr int kJointCount = 200;
    constexpr float kStep = 0.32f;
    constexpr double kYawDeg = 7.0;

    auto obj = std::make_shared<ObjectData>();
    obj->joints.resize(kJointCount);
    obj->vectors4.resize(kJointCount);
    obj->vectors5.resize(kJointCount);
    obj->vectors6.resize(kJointCount);

    std::vector<float> bindZ(kJointCount, 0.0f);
    const int yawQ14 = EncodeEulerDegreesQ14(kYawDeg);

    for (int k = 0; k < kJointCount; ++k) {
        auto& j = obj->joints[k];
        j.id = int16_t(k);
        j.parent = (k == 0) ? int16_t(-1) : int16_t(k - 1);
        j.name = "seg" + std::to_string(k);
        j.isQuaternion = false;
        j.isSkinned = true;

        float localZ = (k == 0) ? 0.0f : kStep;
        obj->vectors4[k] = glm::vec4(0.0f, 0.0f, localZ, 1.0f);
        obj->vectors6[k] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

        // Root has no parent to advance from; every other joint advances
        // 0.32 along the parent's +Z and yaws 7 degrees (about Y) --
        // rotating the "forward" direction each step so the chain curls
        // into a deterministic spiral (glm::perspective/lookAt-style trig
        // at build-corpus time is fine per the plan's global constraints).
        int yaw = (k == 0) ? 0 : yawQ14;
        obj->vectors5[k] = glm::ivec4(0, yaw, 0, 0);

        bindZ[k] = (k == 0) ? 0.0f : bindZ[k - 1] + kStep;
        glm::mat4 bindWorld = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, bindZ[k]));
        j.bindToJointMat = glm::inverse(bindWorld);
    }
    scene.skeleton = obj;

    for (int k = 0; k < kJointCount; ++k) {
        std::vector<float> zLevels = {bindZ[k] - 0.15f, bindZ[k] + 0.15f};
        BoxGeometry geo = BuildRingedBox(0.05f, 0.05f, zLevels);

        MeshPart part;
        part.name = "seg" + std::to_string(k);
        part.materialId = 0;
        part.jointMap = {uint16_t(k)};
        part.useBindToJoint = true;

        for (const auto& src : geo.vertices) {
            GpuVertex v = src;
            v.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            v.boneIndices = glm::uvec4(0u, 0u, 0u, 0u); // local index 0 -> jointMap[0] == k
            part.vertices.push_back(v);
        }
        part.indices = geo.indices;
        scene.meshParts.push_back(std::move(part));
    }

    cs.view = glm::lookAt(glm::vec3(12.0f, -12.0f, 8.0f), glm::vec3(0.0f, 0.0f, 4.0f),
                           glm::vec3(0.0f, 0.0f, 1.0f));
    cs.proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
    return cs;
}

// ── corpus ───────────────────────────────────────────────────────────────

std::vector<CorpusScene> BuildCorpus() {
    std::vector<CorpusScene> corpus;
    corpus.push_back(BuildSphereGrid());
    corpus.push_back(BuildSkinnedCube());
    corpus.push_back(BuildBlendStack());
    corpus.push_back(BuildJointChain200());

    // Fifth scene: the same sphere grid, rendered in ShadingMode::Textured
    // instead of Solid so the goldens actually pin the PBR path (Solid's
    // shader path never reads uMetallic/normal/AO/gloss/scatter -- see
    // Source/Rendering/ShaderManager.cpp). Reuses BuildSphereGrid() rather
    // than duplicating the builder; only name and mode differ.
    CorpusScene textured = BuildSphereGrid();
    textured.name = "sphere-grid-textured";
    textured.mode = Rendering::ShadingMode::Textured;
    corpus.push_back(std::move(textured));

    return corpus;
}

} // namespace Onyx::OracleTool
