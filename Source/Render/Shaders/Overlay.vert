#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

layout(std140, set = 0, binding = 0) uniform OverlayUBO {
    mat4 uViewProj;
};

layout(location = 0) out vec4 vColor;

void main() {
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
