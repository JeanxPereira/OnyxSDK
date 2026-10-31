#version 450

layout(push_constant) uniform Constants {
    vec4 color;
    float depth;
} pc;

void main() {
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, pc.depth, 1.0);
}
