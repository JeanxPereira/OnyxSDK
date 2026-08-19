#version 450

// Vertex-input layout is a straight port of the GL attribute table
// (Include/Onyx/Domain/MeshVertex.h / Source/Rendering/GpuMesh.cpp) so
// mesh upload stays a single source of truth across GL and Vulkan; see
// scene.frag for the full list of intentional divergences this port made.
layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec2  aUV;
layout(location = 3) in vec4  aColor;
layout(location = 4) in vec2  aUV1;
layout(location = 5) in vec4  aBoneWeights;
layout(location = 6) in uvec4 aBoneIndices;
layout(location = 7) in vec4  aTangent;

// set 0 = per-frame (Include/Onyx/RenderVk/Pipelines.h: SceneFrameUBO).
layout(std140, set = 0, binding = 0) uniform FrameUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uCameraPos;
    int  uShadingMode;   // 0 = Solid, 2 = Textured — see scene.frag divergence 3
};

// set 1 = per-batch material (Include/Onyx/RenderVk/Pipelines.h:
// SceneMaterialUBO). Flags bit layout mirrors Onyx::RenderVk::SceneFlags.
layout(std140, set = 1, binding = 0) uniform MaterialUBO {
    vec4  uBaseColor;
    vec4  uLayerColor;
    vec2  uUvOffset;
    float uMetallic;
    uint  uFlags;
};

const uint FLAG_USE_JOINTS = 1u << 5;

// set 1, binding 2: skinning palette. std430 SSBO, unbounded — replaces
// GL's fixed `uniform mat4 uJoints[150]` (scene.frag divergence 8's
// counterpart: this is the mechanical repacking of uUseJoints onto
// uFlags). Per the brief, this is bound even for unskinned batches, with
// an identity entry, as a safety net -- the FLAG_USE_JOINTS branch below
// still governs whether it is read, exactly like GL's uUseJoints branch.
layout(std430, set = 1, binding = 2) readonly buffer JointPalette {
    mat4 uJoints[];
};

// Push constant: per-draw model matrix (replaces GL's uModelTransform
// uniform). Fixed at 64 bytes per the brief's descriptor scheme.
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
        // A weighted sum over the four influences. GOW2 only ever fills two
        // of them - Weight in .x for boneIndices.x and 1-Weight in .y for
        // boneIndices.y, with .zw left at zero - so this reproduces the old
        // two-bone blend exactly for those meshes while letting GOWR, which
        // ships four weights, contribute all of them instead of having half
        // its influence silently dropped. (Identical to the GL port.)
        mat4 skin = uJoints[aBoneIndices.x] * aBoneWeights.x
                  + uJoints[aBoneIndices.y] * aBoneWeights.y
                  + uJoints[aBoneIndices.z] * aBoneWeights.z
                  + uJoints[aBoneIndices.w] * aBoneWeights.w;

        // Guard the degenerate vertex whose weights are all zero: without
        // this the matrix collapses to zero and the vertex lands on the
        // origin.
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

    // Always apply the model transform (handles Z-flip for GOW2 models).
    vec4 worldPos    = uModel * localPos;
    vec3 worldNormal = mat3(uModel) * localNormal;
    vDet = determinant(mat3(uModel));

    vWorldPos    = worldPos.xyz;
    vWorldNormal = normalize(worldNormal);
    vViewNormal  = mat3(uView) * vWorldNormal;
    vViewPos     = (uView * worldPos).xyz;

    // The model transform may mirror (GOW2 flips Z), which flips handedness
    // with it; vDet carries the sign so the fragment stage can correct.
    vWorldTangent = mat3(uModel) * localTangent;
    vHandedness   = aTangent.w;

    vUV  = aUV + uUvOffset;
    vUV1 = aUV1;

    // PS2 vertex color: 128 = full brightness, allow overbright.
    vColor   = aColor * 2.0;
    vColor.a = clamp(aColor.a, 0.0, 1.0);

    // Clip-space Y/Z conventions differ from GL (GLM_FORCE_DEPTH_ZERO_TO_ONE
    // + a Y-flip baked into the projection matrix itself — see the "Camera
    // convention" note in Include/Onyx/RenderVk/Pipelines.h); this line is
    // otherwise byte-identical to the GL source and performs no flip itself.
    gl_Position = uProjection * uView * worldPos;
}
