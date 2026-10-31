#include <Onyx/TestKit/SceneFixtures.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>

namespace Onyx::TestKit {

namespace {

using Domain::BlendMode;
using Domain::TextureRole;

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 weights{0.0f};
    glm::u16vec4 joints{0};
};

struct Geometry {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// The corpus materials were rendered with the shader's fixed vertex colour doubling and with
// reconstructed normal z, so every fixture material states both.
Domain::Material MakeMaterial() {
    Domain::Material mat;
    mat.vertexColorScale = 2.0f;
    mat.normalEncoding = Domain::NormalEncoding::ReconstructZ;
    return mat;
}

Domain::MeshPart ToPart(std::string name, const Geometry& geo, uint32_t materialIndex, bool skinned) {
    Domain::MeshPart part;
    part.name = std::move(name);
    part.materialIndex = materialIndex;
    for (const Vertex& v : geo.vertices) {
        part.positions.push_back(v.position);
        part.normals.push_back(v.normal);
        part.uv0.push_back(v.uv);
        part.uv1.emplace_back(0.0f);
        part.colors.emplace_back(1.0f);
        part.tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
        if (skinned) {
            part.joints.push_back(v.joints);
            part.weights.push_back(v.weights);
        }
    }
    part.indices = geo.indices;
    part.bounds = Domain::BoundingBox::Of(part.positions);
    return part;
}

// Stacks x slices UV sphere; the pole rows degenerate to zero-area triangles.
Geometry BuildUvSphere(float radius, int stacks, int slices, glm::vec3 center) {
    Geometry geo;
    const float pi = glm::pi<float>();
    const int slicesV = slices + 1;

    for (int stack = 0; stack <= stacks; ++stack) {
        const float v = float(stack) / float(stacks);
        const float phi = v * pi;
        const float z = radius * std::cos(phi);
        const float r = radius * std::sin(phi);
        for (int slice = 0; slice <= slices; ++slice) {
            const float u = float(slice) / float(slices);
            const float theta = u * 2.0f * pi;
            const float x = r * std::cos(theta);
            const float y = r * std::sin(theta);

            Vertex vert;
            vert.position = glm::vec3(x, y, z) + center;
            vert.normal = radius > 0.0f ? glm::vec3(x, y, z) / radius : glm::vec3(0.0f, 0.0f, 1.0f);
            vert.uv = glm::vec2(u, v);
            geo.vertices.push_back(vert);
        }
    }

    for (int stack = 0; stack < stacks; ++stack) {
        for (int slice = 0; slice < slices; ++slice) {
            const uint32_t i0 = uint32_t(stack * slicesV + slice);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + uint32_t(slicesV);
            const uint32_t i3 = i2 + 1;
            geo.indices.insert(geo.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    return geo;
}

// A halfX x halfY box extruded through ascending z levels: one four-vertex ring per level with
// radial wall normals, plus +-Z cap vertices at the first and last level.
Geometry BuildRingedBox(float halfX, float halfY, const std::vector<float>& zLevels) {
    Geometry geo;
    const size_t nRings = zLevels.size();

    static const std::array<glm::vec2, 4> kCorner = {glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f),
                                                     glm::vec2(1.0f, 1.0f), glm::vec2(-1.0f, 1.0f)};

    for (size_t r = 0; r < nRings; ++r) {
        const float t = nRings > 1 ? float(r) / float(nRings - 1) : 0.0f;
        for (int c = 0; c < 4; ++c) {
            Vertex v;
            v.position = glm::vec3(kCorner[c].x * halfX, kCorner[c].y * halfY, zLevels[r]);
            v.normal = glm::normalize(glm::vec3(kCorner[c].x, kCorner[c].y, 0.0f));
            v.uv = glm::vec2(float(c) / 4.0f, t);
            geo.vertices.push_back(v);
        }
    }
    for (size_t r = 0; r + 1 < nRings; ++r) {
        for (uint32_t c = 0; c < 4; ++c) {
            const uint32_t c1 = (c + 1) % 4;
            const uint32_t i0 = uint32_t(r * 4 + c);
            const uint32_t i1 = uint32_t(r * 4 + c1);
            const uint32_t i2 = uint32_t((r + 1) * 4 + c1);
            const uint32_t i3 = uint32_t((r + 1) * 4 + c);
            geo.indices.insert(geo.indices.end(), {i0, i1, i2, i0, i2, i3});
        }
    }

    auto addCap = [&](float z, float nz, bool bottom) {
        const uint32_t base = uint32_t(geo.vertices.size());
        for (int c = 0; c < 4; ++c) {
            Vertex v;
            v.position = glm::vec3(kCorner[c].x * halfX, kCorner[c].y * halfY, z);
            v.normal = glm::vec3(0.0f, 0.0f, nz);
            v.uv = glm::vec2(kCorner[c].x * 0.5f + 0.5f, kCorner[c].y * 0.5f + 0.5f);
            geo.vertices.push_back(v);
        }
        if (bottom) {
            geo.indices.insert(geo.indices.end(), {base + 0, base + 2, base + 1, base + 0, base + 3, base + 2});
        } else {
            geo.indices.insert(geo.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
        }
    };
    addCap(zLevels.front(), -1.0f, true);
    addCap(zLevels.back(), 1.0f, false);
    return geo;
}

// The frozen corpus stores Euler angles as Q.14 fractions of a turn; the same quantisation is applied
// here so the rest pose matches bit for bit. The rotation is about a single axis (0 = X, 1 = Y).
glm::quat QuantisedAxisRotation(double degrees, int axis) {
    const long q14 = std::lround(degrees / 360.0 * 16384.0);
    const float half = float(q14) * (1.0f / 16384.0f) * 360.0f * (0.5f * glm::pi<float>() / 180.0f);
    const float s = std::sin(half);
    const float c = std::cos(half);
    glm::quat q(c, axis == 0 ? s : 0.0f, axis == 1 ? s : 0.0f, 0.0f);
    return glm::normalize(q);
}

Domain::Joint MakeJoint(std::string name, int32_t parent, float localZ, const glm::quat& rotation, float bindZ) {
    Domain::Joint joint;
    joint.name = std::move(name);
    joint.parent = parent;
    joint.localBind = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, localZ)) * glm::mat4_cast(rotation);
    joint.inverseBind = glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, bindZ)));
    return joint;
}

void FinishScene(SceneFixture& fixture) {
    fixture.scene.bounds = fixture.scene.ComputeBounds();
}

SceneFixture BuildSphereGridNamed(std::string name, bool textured) {
    SceneFixture f;
    f.name = std::move(name);
    f.textured = textured;
    Domain::Scene& scene = f.scene;

    scene.textures.push_back(MakeChecker(64, {255, 0, 0, 255}, {255, 255, 255, 255}, "sphere_diffuse_checker"));
    scene.textures.push_back(MakeBumpNormal(64, "sphere_normal_bump"));
    scene.textures.push_back(MakeGradient(64, {255, 255, 255, 255}, {128, 128, 128, 255}, "sphere_occlusion_grad"));
    scene.textures.push_back(MakeGradient(64, {0, 0, 0, 255}, {255, 255, 255, 255}, "sphere_gloss_grad"));
    scene.textures.push_back(MakeSolid(64, {128, 128, 128, 255}, "sphere_height_solid"));
    scene.textures.push_back(MakeSolid(64, {200, 60, 60, 255}, "sphere_scatter_solid"));
    scene.textures.push_back(MakeChecker(32, {0, 0, 255, 255}, {255, 255, 255, 255}, "sphere_detail_checker"));
    scene.textures.push_back(MakeSolid(64, {0, 0, 0, 255}, "sphere_emissive_black"));
    scene.textures.push_back(MakeSolid(64, {0, 80, 0, 255}, "sphere_emissive_green"));
    scene.textures.push_back(MakeGradient(64, {135, 206, 235, 255}, {255, 255, 255, 255}, "sphere_envmap_grad"));

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const float x = -2.0f + 2.0f * float(col);
            const float y = -2.0f + 2.0f * float(row);
            const float metallic = float(col) * 0.5f;

            const Geometry geo = BuildUvSphere(0.8f, 24, 32, glm::vec3(x, y, 0.0f));
            scene.parts.push_back(ToPart("sphere_c" + std::to_string(col) + "_r" + std::to_string(row) +
                                             "_metallic" + std::to_string(metallic),
                                         geo, uint32_t(row * 3 + col), false));

            Domain::Material mat = MakeMaterial();
            mat.name = "sphere_material_" + std::to_string(row * 3 + col);
            mat.baseColor = glm::vec4(0.4f + 0.3f * float(col), 0.4f + 0.3f * float(row), 0.6f, 1.0f);
            mat.blend = BlendMode::Opaque;
            mat.metallic = metallic;
            mat.textures[TextureRole::Diffuse] = 0;
            mat.textures[TextureRole::Normal] = 1;
            mat.textures[TextureRole::Occlusion] = 2;
            mat.textures[TextureRole::Gloss] = 3;
            mat.textures[TextureRole::Height] = 4;
            mat.textures[TextureRole::Scatter] = 5;
            mat.textures[TextureRole::Detail] = 6;
            mat.textures[TextureRole::Emissive] = row == 2 ? 8 : 7;
            mat.textures[TextureRole::EnvMap] = 9;
            scene.materials.push_back(std::move(mat));
        }
    }

    f.view = glm::lookAt(glm::vec3(0.0f, -7.0f, 2.5f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    FinishScene(f);
    return f;
}

}

SceneFixture BuildSphereGrid() {
    return BuildSphereGridNamed("sphere-grid", false);
}

SceneFixture BuildSkinnedCube() {
    SceneFixture f;
    f.name = "skinned-cube";
    Domain::Scene& scene = f.scene;

    scene.textures.push_back(MakeChecker(64, {255, 165, 0, 255}, {255, 255, 255, 255}, "cube_diffuse_checker"));

    Domain::Material mat = MakeMaterial();
    mat.name = "cube_material";
    mat.textures[TextureRole::Diffuse] = 0;
    scene.materials.push_back(std::move(mat));

    // Bind pose is a straight chain; the rest pose bends joints 1 and 2 by 30 degrees about X.
    constexpr int kJointCount = 3;
    const float bindZ[kJointCount] = {0.0f, 1.33f, 2.67f};

    auto skeleton = std::make_shared<Domain::Skeleton>();
    for (int i = 0; i < kJointCount; ++i) {
        const float localZ = i == 0 ? bindZ[0] : bindZ[i] - bindZ[i - 1];
        const glm::quat rest = (i == 1 || i == 2) ? QuantisedAxisRotation(30.0, 0) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        skeleton->joints.push_back(MakeJoint("joint" + std::to_string(i), i == 0 ? -1 : i - 1, localZ, rest, bindZ[i]));
    }
    scene.skeleton = std::move(skeleton);

    std::vector<float> zLevels;
    for (int i = 0; i < 8; ++i) zLevels.push_back(4.0f * float(i) / 7.0f);
    Geometry geo = BuildRingedBox(0.5f, 0.5f, zLevels);

    for (Vertex& v : geo.vertices) {
        float d[kJointCount];
        for (int k = 0; k < kJointCount; ++k) d[k] = std::fabs(v.position.z - bindZ[k]);
        std::array<int, kJointCount> order = {0, 1, 2};
        std::sort(order.begin(), order.end(), [&](int p, int q) { return d[p] < d[q]; });
        const int a = order[0];
        const int b = order[1];
        const float da = d[a];
        const float db = d[b];
        float wa = 1.0f;
        float wb = 0.0f;
        if (da + db > 1e-8f) {
            wa = db / (da + db);
            wb = da / (da + db);
        }
        v.weights = glm::vec4(wa, wb, 0.0f, 0.0f);
        v.joints = glm::u16vec4(uint16_t(a), uint16_t(b), 0, 0);
    }

    Domain::MeshPart part = ToPart("cube_body", geo, 0, true);
    part.jointMap = {0, 1, 2};
    scene.parts.push_back(std::move(part));

    f.view = glm::lookAt(glm::vec3(4.0f, -6.0f, 2.0f), glm::vec3(0.0f, 0.0f, 1.5f), glm::vec3(0.0f, 0.0f, 1.0f));
    FinishScene(f);
    return f;
}

SceneFixture BuildAnimatedChain() {
    SceneFixture f = BuildSkinnedCube();
    f.name = "animated-chain";

    // The frozen corpus clip sweeps joints 1 and 2 about X from 60 to 120 degrees; the keys here start and end
    // at the rest rotation, so the clip at 0 s reproduces the rest pose, and reach the mid-sweep
    // angle of 90 degrees at 0.5 s.
    const glm::quat rest = QuantisedAxisRotation(30.0, 0);
    const glm::quat mid = QuantisedAxisRotation(90.0, 0);

    Domain::AnimClip clip;
    clip.name = "bend";
    clip.duration = 1.0f;
    for (uint32_t joint = 1; joint <= 2; ++joint) {
        Domain::AnimChannel channel;
        channel.joint = joint;
        channel.rotation = {{0.0f, rest}, {0.5f, mid}, {1.0f, rest}};
        clip.channels.push_back(std::move(channel));
    }

    auto set = std::make_shared<Domain::AnimationSet>();
    set->clips.push_back(std::move(clip));
    f.scene.animations = std::move(set);
    return f;
}

SceneFixture BuildBlendStack() {
    SceneFixture f;
    f.name = "blend-stack";
    Domain::Scene& scene = f.scene;

    scene.textures.push_back(MakeChecker(64, {128, 128, 128, 255}, {255, 255, 255, 255}, "floor_checker"));
    scene.textures.push_back(MakeSolid(64, {255, 255, 255, 255}, "quad_normal_white"));
    scene.textures.push_back(MakeSolid(64, {40, 40, 255, 255}, "quad_additive_blue"));
    scene.textures.push_back(MakeSolid(64, {60, 60, 60, 255}, "quad_subtractive_grey"));

    auto addMaterial = [&](std::string name, BlendMode blend, float alpha, uint32_t texture) {
        Domain::Material mat = MakeMaterial();
        mat.name = std::move(name);
        mat.blend = blend;
        mat.baseColor.a = alpha;
        mat.textures[TextureRole::Diffuse] = texture;
        scene.materials.push_back(std::move(mat));
    };
    addMaterial("floor", BlendMode::Opaque, 1.0f, 0);
    addMaterial("quad_normal", BlendMode::Opaque, 0.5f, 1);
    addMaterial("quad_additive", BlendMode::Additive, 1.0f, 2);
    addMaterial("quad_subtractive", BlendMode::Subtractive, 1.0f, 3);

    auto makeQuad = [](float halfX, float halfY, float z) {
        static const std::array<glm::vec2, 4> kCorner = {glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f),
                                                         glm::vec2(1.0f, 1.0f), glm::vec2(-1.0f, 1.0f)};
        static const std::array<glm::vec2, 4> kUv = {glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f),
                                                     glm::vec2(1.0f, 1.0f), glm::vec2(0.0f, 1.0f)};
        Geometry geo;
        for (int i = 0; i < 4; ++i) {
            Vertex v;
            v.position = glm::vec3(kCorner[i].x * halfX, kCorner[i].y * halfY, z);
            v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            v.uv = kUv[i];
            geo.vertices.push_back(v);
        }
        geo.indices = {0, 1, 2, 0, 2, 3};
        return geo;
    };

    scene.parts.push_back(ToPart("floor", makeQuad(3.0f, 3.0f, 0.0f), 0, false));
    scene.parts.push_back(ToPart("quad_normal", makeQuad(1.0f, 1.0f, 0.5f), 1, false));
    scene.parts.push_back(ToPart("quad_additive", makeQuad(1.0f, 1.0f, 1.0f), 2, false));
    scene.parts.push_back(ToPart("quad_subtractive", makeQuad(1.0f, 1.0f, 1.5f), 3, false));

    f.view = glm::lookAt(glm::vec3(0.0f, -6.0f, 4.0f), glm::vec3(0.0f, 0.0f, 0.8f), glm::vec3(0.0f, 0.0f, 1.0f));
    FinishScene(f);
    return f;
}

SceneFixture BuildJointChain200() {
    SceneFixture f;
    f.name = "joint-chain-200";
    Domain::Scene& scene = f.scene;

    scene.textures.push_back(MakeSolid(64, {200, 200, 60, 255}, "chain_diffuse_solid"));
    Domain::Material mat = MakeMaterial();
    mat.name = "chain_material";
    mat.textures[TextureRole::Diffuse] = 0;
    scene.materials.push_back(std::move(mat));

    constexpr int kJointCount = 200;
    constexpr float kStep = 0.32f;
    constexpr double kYawDegrees = 7.0;

    auto skeleton = std::make_shared<Domain::Skeleton>();
    std::vector<float> bindZ(kJointCount, 0.0f);
    for (int k = 0; k < kJointCount; ++k) {
        bindZ[k] = k == 0 ? 0.0f : bindZ[k - 1] + kStep;
        const float localZ = k == 0 ? 0.0f : kStep;
        const glm::quat yaw = k == 0 ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : QuantisedAxisRotation(kYawDegrees, 1);
        skeleton->joints.push_back(MakeJoint("seg" + std::to_string(k), k == 0 ? -1 : k - 1, localZ, yaw, bindZ[k]));
    }
    scene.skeleton = std::move(skeleton);

    for (int k = 0; k < kJointCount; ++k) {
        Geometry geo = BuildRingedBox(0.05f, 0.05f, {bindZ[k] - 0.15f, bindZ[k] + 0.15f});
        for (Vertex& v : geo.vertices) {
            v.weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            v.joints = glm::u16vec4(0);
        }
        Domain::MeshPart part = ToPart("seg" + std::to_string(k), geo, 0, true);
        part.jointMap = {uint16_t(k)};
        scene.parts.push_back(std::move(part));
    }

    f.view = glm::lookAt(glm::vec3(12.0f, -12.0f, 8.0f), glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    FinishScene(f);
    return f;
}

std::vector<SceneFixture> BuildCorpus() {
    std::vector<SceneFixture> corpus;
    corpus.push_back(BuildSphereGrid());
    corpus.push_back(BuildSphereGridNamed("sphere-grid-textured", true));
    corpus.push_back(BuildSkinnedCube());
    corpus.push_back(BuildBlendStack());
    corpus.push_back(BuildJointChain200());
    return corpus;
}

}
