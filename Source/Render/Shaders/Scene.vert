#version 450

layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec2  aUV;
layout(location = 3) in vec4  aColor;
layout(location = 4) in vec2  aUV1;
layout(location = 5) in vec4  aBoneWeights;
layout(location = 6) in uvec4 aBoneIndices;
layout(location = 7) in vec4  aTangent;

layout(std140, set = 0, binding = 0) uniform FrameUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uCameraPos;
    int  uShadingMode;
};

layout(std140, set = 1, binding = 0) uniform MaterialUBO {
    vec4  uBaseColor;
    vec4  uLayerColor;
    vec2  uUvOffset;
    float uMetallic;
    uint  uFlags;
    vec4  uNormalDecode;
};

const uint FLAG_USE_JOINTS = 1u << 5;

layout(std430, set = 1, binding = 2) readonly buffer JointPalette {
    mat4 uJoints[];
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
        mat4 skin = uJoints[aBoneIndices.x] * aBoneWeights.x
                  + uJoints[aBoneIndices.y] * aBoneWeights.y
                  + uJoints[aBoneIndices.z] * aBoneWeights.z
                  + uJoints[aBoneIndices.w] * aBoneWeights.w;

        float wsum = aBoneWeights.x + aBoneWeights.y + aBoneWeights.z + aBoneWeights.w;
        if (wsum < 0.001) skin = uJoints[aBoneIndices.x];

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

    vColor   = aColor * 2.0;
    vColor.a = clamp(aColor.a, 0.0, 1.0);

    gl_Position = uProjection * uView * worldPos;
}
