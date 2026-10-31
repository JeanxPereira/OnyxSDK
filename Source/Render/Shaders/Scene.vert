#version 450

// Interleaved vertex, 72 bytes: position R32G32B32 @0, normal R16G16B16A16_SNORM @12, tangent
// R16G16B16A16_SNORM @20 (w = handedness), uv0 @28, uv1 @36, colour R8G8B8A8_UNORM @44,
// joints R16G16B16A16_UINT @48, weights R32G32B32A32 @56.
layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec4  aTangent;
layout(location = 3) in vec2  aUV;
layout(location = 4) in vec2  aUV1;
layout(location = 5) in vec4  aColor;
layout(location = 6) in uvec4 aBoneIndices;
layout(location = 7) in vec4  aBoneWeights;

layout(std140, set = 0, binding = 0) uniform FrameUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uCameraPos;
    int  uShadingMode;
};

// Global skinning palette of the frame: one matrix per skeleton joint, never empty.
layout(std430, set = 0, binding = 1) readonly buffer JointPalette {
    mat4 uJoints[];
};

// Flags bit layout: 0 texture, 1 normal map, 2 AO, 3 gloss, 4 scatter,
// 5 joints, 6 environment map, 7 opacity, 8 XYZ normal map.
layout(std140, set = 1, binding = 0) uniform MaterialUBO {
    vec4  uBaseColor;
    vec4  uLayerColor;
    vec2  uUvOffset;
    float uMetallic;
    uint  uFlags;
    vec4  uNormalDecode;
    float uVertexColorScale;
};

const uint FLAG_USE_JOINTS = 1u << 5;

// Per part: the slot of each local joint index in the global palette. Bound for every part;
// FLAG_USE_JOINTS decides whether it is read.
layout(std430, set = 2, binding = 0) readonly buffer JointMap {
    uint uJointMap[];
};

layout(push_constant) uniform PushConstants {
    mat4 uModel;
};

layout(location = 0) out vec3  vWorldPos;
layout(location = 1) out vec3  vWorldNormal;
layout(location = 2) out vec3  vViewNormal;
layout(location = 3) out vec3  vViewPos;
layout(location = 4) out vec2  vUV;
layout(location = 5) out vec2  vUV1;
layout(location = 6) out vec4  vColor;
layout(location = 7) out float vDet;
layout(location = 8) out vec3  vWorldTangent;
layout(location = 9) out float vHandedness;

void main() {
    vec4 localPos;
    vec3 localNormal;
    vec3 localTangent;

    if ((uFlags & FLAG_USE_JOINTS) != 0u) {
        mat4 skin = uJoints[uJointMap[aBoneIndices.x]] * aBoneWeights.x
                  + uJoints[uJointMap[aBoneIndices.y]] * aBoneWeights.y
                  + uJoints[uJointMap[aBoneIndices.z]] * aBoneWeights.z
                  + uJoints[uJointMap[aBoneIndices.w]] * aBoneWeights.w;

        float wsum = aBoneWeights.x + aBoneWeights.y + aBoneWeights.z + aBoneWeights.w;
        // A vertex whose weights are all zero would collapse to the origin.
        if (wsum < 0.001) skin = uJoints[uJointMap[aBoneIndices.x]];

        localPos     = skin * vec4(aPos, 1.0);
        localNormal  = mat3(skin) * aNormal;
        localTangent = mat3(skin) * aTangent.xyz;
    } else {
        localPos     = vec4(aPos, 1.0);
        localNormal  = aNormal;
        localTangent = aTangent.xyz;
    }

    vec4 worldPos    = uModel * localPos;
    vec3 worldNormal = mat3(uModel) * localNormal;
    vDet = determinant(mat3(uModel));

    vWorldPos    = worldPos.xyz;
    vWorldNormal = normalize(worldNormal);
    vViewNormal  = mat3(uView) * vWorldNormal;
    vViewPos     = (uView * worldPos).xyz;

    vWorldTangent = mat3(uModel) * localTangent;
    vHandedness   = aTangent.w;

    vUV  = aUV + uUvOffset;
    vUV1 = aUV1;

    // The material states how its vertex colours are scaled: a format that stores colours at
    // half brightness sets 2, and overbright is allowed.
    vColor = vec4(aColor.rgb * uVertexColorScale, aColor.a);

    gl_Position = uProjection * uView * worldPos;
}
