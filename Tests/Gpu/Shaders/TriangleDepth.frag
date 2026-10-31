#version 450

layout(push_constant) uniform Constants {
    vec4 color;
    float depth;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = pc.color;
}
