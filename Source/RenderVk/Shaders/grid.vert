#version 450

// Fullscreen triangle generated procedurally, identical to GL's GRID_VERT
// (Source/Rendering/ShaderManager.cpp:352+) except gl_VertexID becomes
// gl_VertexIndex — the GLSL 450/Vulkan spelling of the same built-in, not
// a behavior change. No vertex buffer is bound for this pipeline.
layout(location = 0) out vec2 vUV;

void main() {
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
