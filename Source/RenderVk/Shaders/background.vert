#version 450

// Fullscreen triangle, identical to GL's BG_VERT (Source/Rendering/
// ShaderManager.cpp:570+) except gl_VertexID -> gl_VertexIndex (GLSL
// 450/Vulkan naming) and the constant Z. GL wrote 0.999 in its [-1,1]
// clip convention (== depth 0.9995 once mapped to the [0,1] depth
// buffer); depth test AND depth write are disabled for this pass in both
// APIs (SceneRenderer::RenderBackground / the BackgroundPipeline in
// Include/Onyx/RenderVk/Pipelines.h), so the exact value is inert either
// way — written as the equivalent [0,1] depth below for hygiene, not
// because it is load-bearing.
layout(location = 0) out vec2 vUV;

void main() {
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.9995, 1.0);
}
