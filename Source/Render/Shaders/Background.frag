#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

layout(std140, set = 0, binding = 0) uniform BackgroundUBO {
    vec4 uTopColor;
    vec4 uBottomColor;
};

void main() {
    float t = 1.0 - vUV.y;
    t = t * t * (3.0 - 2.0 * t);
    FragColor = vec4(mix(uBottomColor.rgb, uTopColor.rgb, t), 1.0);
}
