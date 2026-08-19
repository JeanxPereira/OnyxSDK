#version 450

// Ports GL's BG_FRAG (Source/Rendering/ShaderManager.cpp:570+) unchanged
// except uTopColor/uBottomColor moving from separate uniforms into a
// single set 0 / binding 0 UBO (mechanical; alpha channel is present only
// to keep the UBO's C++ mirror struct a plain vec4 pair — the shader
// still only reads .rgb, exactly like GL) and the Y-flip fix below.
//
// Y-FLIP FIX (fix round, was missing): this pass writes gl_Position
// directly from a procedural fullscreen-triangle UV (background.vert)
// and never touches a projection matrix, so the milestone's projection-
// level Y-flip (Include/Onyx/Rendering/Pipelines.h's "Camera convention"
// note) does not cover it. Concretely: GL's NDC has y=+1 at the top of
// the screen, so background.vert's raw vUV.y=1 vertex lands at the
// screen top, and this shader's original `t = vUV.y` correctly put the
// top color there. Vulkan's NDC has y=-1 at the top instead (the exact
// asymmetry the projection Y-flip exists to correct elsewhere), so the
// SAME vUV.y=1 vertex now lands at the screen BOTTOM under Vulkan's
// native (unflipped-viewport) rasterization — the gradient rendered
// upside down until this line was added. `t = 1.0 - vUV.y` re-anchors
// the gradient to the physically correct screen row without touching
// vert-stage geometry or the UBO. grid.vert/grid.frag do the same
// NDC-direct trick but do NOT need this fix — grid.frag immediately
// unprojects its raw NDC through `uInvViewProj` (the inverse of the
// ALREADY-flip-corrected view-projection), which cancels the same
// asymmetry back out; background has no such round-trip; see this file's
// use here and Pipelines.h's "Camera convention" note for the full
// argument.
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

layout(std140, set = 0, binding = 0) uniform BackgroundUBO {
    vec4 uTopColor;
    vec4 uBottomColor;
};

void main() {
    // Smooth gradient from bottom to top. `1.0 - vUV.y`, not `vUV.y` —
    // see the Y-FLIP FIX comment above.
    float t = 1.0 - vUV.y;
    t = t * t * (3.0 - 2.0 * t); // smoothstep-like curve
    FragColor = vec4(mix(uBottomColor.rgb, uTopColor.rgb, t), 1.0);
}
