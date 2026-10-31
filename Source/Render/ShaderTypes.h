#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace Onyx::Render::Shader {

// Set 0, binding 0 of the scene shaders (std140).
struct FrameUbo {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 cameraPos;
    int32_t shadingMode;
};
static_assert(sizeof(FrameUbo) == 144);
static_assert(offsetof(FrameUbo, view) == 0);
static_assert(offsetof(FrameUbo, proj) == 64);
static_assert(offsetof(FrameUbo, cameraPos) == 128);
static_assert(offsetof(FrameUbo, shadingMode) == 140);

inline constexpr int32_t kShadingSolid = 0;
inline constexpr int32_t kShadingTextured = 2;

// Set 1, binding 0 of the scene shaders (std140).
struct MaterialUbo {
    glm::vec4 baseColor;
    glm::vec4 tint;
    glm::vec2 uvOffset;
    float metallic;
    uint32_t flags;
    glm::vec4 normalDecode;
    float vertexColorScale;
    float pad[3];
};
static_assert(sizeof(MaterialUbo) == 80);
static_assert(offsetof(MaterialUbo, baseColor) == 0);
static_assert(offsetof(MaterialUbo, tint) == 16);
static_assert(offsetof(MaterialUbo, uvOffset) == 32);
static_assert(offsetof(MaterialUbo, metallic) == 40);
static_assert(offsetof(MaterialUbo, flags) == 44);
static_assert(offsetof(MaterialUbo, normalDecode) == 48);
static_assert(offsetof(MaterialUbo, vertexColorScale) == 64);

// Set 0, binding 0 of the background shaders (std140).
struct BackgroundUbo {
    glm::vec4 top;
    glm::vec4 bottom;
};
static_assert(sizeof(BackgroundUbo) == 32);
static_assert(offsetof(BackgroundUbo, bottom) == 16);

// Push constant of the scene vertex shader.
struct ScenePush {
    glm::mat4 model;
};
static_assert(sizeof(ScenePush) == 64);

namespace Flag {
inline constexpr uint32_t kUseTexture = 1u << 0;
inline constexpr uint32_t kHasNormal = 1u << 1;
inline constexpr uint32_t kHasAO = 1u << 2;
inline constexpr uint32_t kHasGloss = 1u << 3;
inline constexpr uint32_t kHasScatter = 1u << 4;
inline constexpr uint32_t kUseJoints = 1u << 5;
inline constexpr uint32_t kHasEnvmap = 1u << 6;
inline constexpr uint32_t kHasOpacity = 1u << 7;
inline constexpr uint32_t kNormalXYZ = 1u << 8;
}

// The seven sampled roles, in the binding order of set 1 binding 1.
inline constexpr size_t kRoleCount = 7;

// Interleaved scene vertex, 72 bytes. Unskinned parts carry zero joints and weights.
struct SceneVertex {
    float position[3];
    int16_t normal[4];
    int16_t tangent[4];
    float uv0[2];
    float uv1[2];
    uint8_t color[4];
    uint16_t joints[4];
    float weights[4];
};
static_assert(sizeof(SceneVertex) == 72);
static_assert(offsetof(SceneVertex, position) == 0);
static_assert(offsetof(SceneVertex, normal) == 12);
static_assert(offsetof(SceneVertex, tangent) == 20);
static_assert(offsetof(SceneVertex, uv0) == 28);
static_assert(offsetof(SceneVertex, uv1) == 36);
static_assert(offsetof(SceneVertex, color) == 44);
static_assert(offsetof(SceneVertex, joints) == 48);
static_assert(offsetof(SceneVertex, weights) == 56);

// Every offset into a uniform or storage buffer is a multiple of this, the Vulkan upper bound of
// minUniformBufferOffsetAlignment and minStorageBufferOffsetAlignment.
inline constexpr uint32_t kBufferAlignment = 256;

}
