#version 450

// ═══════════════════════════════════════════════════════════════════════
// Intentional divergences from the GL source this file ports
// (Source/Rendering/ShaderManager.cpp: GRID_FRAG at :352+, driven by
// Source/Rendering/GridRenderer.cpp). The grid math itself — the ray/plane
// intersection, the derivative-based line antialiasing, the axis tint,
// the LOD fade, the distance fade — is a line-for-line port.
//
// NOTE for T6: SceneRenderer::RenderSkeleton (GL) draws its debug
// bone/joint lines through this same "grid" program rather than a
// dedicated shader (confirmed by reading Source/Rendering/
// SceneRenderer.cpp:722-812 — it fetches ShaderManager::Get().
// GetShader("grid") and feeds it uView/uProjection, both of which
// GRID_VERT/GRID_FRAG never declare, so those calls are silently no-ops
// against nonexistent uniform locations). The Vulkan port does the same:
// no overlay.vert/frag pair exists in this task: whatever T6 does for
// skeleton-line drawing reuses this grid pipeline, not a new one.
//
//  1. gl_VertexID -> gl_VertexIndex (grid.vert): GLSL 450/Vulkan naming,
//     not a behavior change.
//  2. Uniforms -> a single set 0 / binding 0 UBO (uViewProj, uInvViewProj,
//     uGridColor, uCameraPos, uGridScale) — mechanical, values unchanged.
//  3. Depth convention. GL's clip space is [-1,1] on Z; this milestone's
//     fixed camera rule (GLM_FORCE_DEPTH_ZERO_TO_ONE — see the "Camera
//     convention" note in Include/Onyx/RenderVk/Pipelines.h) makes
//     Vulkan's [0,1] instead. This shader actively uses NDC Z for
//     occlusion (it writes gl_FragDepth so the grid tests correctly
//     against real geometry, per GridRenderer.cpp's comment), so three
//     spots that assumed [-1,1] are adjusted to land on the SAME world
//     position / depth result as GL, not merely "a" depth:
//       a) the near/far unprojection points use clip Z 0.0 / 1.0 (GL used
//          -1.0 / 1.0 — those were its near/far clip Z under the old
//          convention);
//       b) the discard guard on the reprojected NDC Z is `< 0.0 || > 1.0`
//          (GL: `< -1.0 || > 1.0`);
//       c) gl_FragDepth is written as the NDC Z directly, with no
//          `* 0.5 + 0.5` remap — GL needed that remap because its own NDC
//          Z was [-1,1] while its depth buffer is [0,1]; with
//          GLM_FORCE_DEPTH_ZERO_TO_ONE the NDC Z is already [0,1], so
//          applying the remap again would double-map it.
// ═══════════════════════════════════════════════════════════════════════

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

layout(std140, set = 0, binding = 0) uniform GridUBO {
    mat4  uViewProj;
    mat4  uInvViewProj;
    vec4  uGridColor;
    vec3  uCameraPos;
    float uGridScale;
};

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;

    // Unproject near and far points to get a world-space ray (divergence 3a).
    vec4 nearPt = uInvViewProj * vec4(ndc, 0.0, 1.0);
    vec4 farPt  = uInvViewProj * vec4(ndc, 1.0, 1.0);

    vec3 rayOrigin = nearPt.xyz / nearPt.w;
    vec3 rayEnd    = farPt.xyz / farPt.w;
    vec3 rayDir    = normalize(rayEnd - rayOrigin);

    // Intersect with Y=0 plane
    if (abs(rayDir.y) < 0.0001) discard;

    float t = -rayOrigin.y / rayDir.y;
    if (t < 0.0) discard; // Intersection is behind camera

    vec3 worldPos = rayOrigin + t * rayDir;

    // Calculate accurate depth for occlusion (divergence 3b/3c).
    vec4 clipPos = uViewProj * vec4(worldPos, 1.0);
    float ndcZ = clipPos.z / clipPos.w;
    if (ndcZ < 0.0 || ndcZ > 1.0) discard;
    gl_FragDepth = ndcZ;

    // --- Grid Drawing Math ---
    vec2 coord = worldPos.xz;
    vec2 deriv = max(fwidth(coord), vec2(0.00001));

    vec2 gridCoord = coord / uGridScale;
    vec2 gridDeriv = deriv / uGridScale;
    vec2 gridDist = abs(fract(gridCoord - 0.5) - 0.5) / gridDeriv;
    float gridAlpha = 1.0 - min(min(gridDist.x, gridDist.y), 1.0);

    float xAxisAlpha = 1.0 - min(abs(coord.y) / (deriv.y * 1.5), 1.0);
    float zAxisAlpha = 1.0 - min(abs(coord.x) / (deriv.x * 1.5), 1.0);

    float lodFade = max(0.0, 1.0 - max(gridDeriv.x, gridDeriv.y));
    gridAlpha *= lodFade;

    vec4 color = uGridColor;
    color.a *= gridAlpha;

    if (xAxisAlpha > 0.0) {
        color = mix(color, vec4(0.8, 0.2, 0.2, 0.8), xAxisAlpha);
    }
    if (zAxisAlpha > 0.0) {
        color = mix(color, vec4(0.2, 0.4, 0.8, 0.8), zAxisAlpha);
    }

    if (color.a < 0.01) discard;

    float d = length(worldPos - uCameraPos);
    float fade = 1.0 - smoothstep(0.0, 50000.0, d);

    if (uCameraPos.y < 0.0) {
        fade *= smoothstep(-10.0, 0.0, uCameraPos.y);
    }

    color.a *= fade;
    FragColor = color;
}
