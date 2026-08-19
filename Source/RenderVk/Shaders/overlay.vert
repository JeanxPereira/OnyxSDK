#version 450

// Overlay line pipeline (T6's skeleton/gizmo debug drawing) — see the
// "NOTE on skeleton/gizmo debug lines" block at the top of grid.frag for
// why this pair exists (a fix-round addition; an earlier pass wrongly
// tried to reuse the grid pipeline instead).
//
// Mirrors GL SceneRenderer::RenderSkeleton's own vertex setup exactly
// (Source/Rendering/SceneRenderer.cpp:725-793): a local `LineVert { vec3
// pos; vec4 color; }` buffer of already-world-space points (RenderSkeleton
// bakes m_instanceTransform into every point itself before pushing it),
// drawn GL_LINES. Onyx::RenderVk::OverlayVertex (Pipelines.h) mirrors the
// same two-field layout for the Vulkan vertex input state.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

// set 0, binding 0 — mirrors Onyx::RenderVk::OverlayUBO (Pipelines.h,
// std140). Only a combined view*proj is needed: aPos is already
// world-space, so unlike the scene pipelines there is no per-instance
// model matrix / push constant here.
layout(std140, set = 0, binding = 0) uniform OverlayUBO {
    mat4 uViewProj;
};

layout(location = 0) out vec4 vColor;

void main() {
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
