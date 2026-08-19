#version 450

// Ports GL's BG_FRAG (Source/Rendering/ShaderManager.cpp:570+) unchanged
// except uTopColor/uBottomColor moving from separate uniforms into a
// single set 0 / binding 0 UBO (mechanical; alpha channel is present only
// to keep the UBO's C++ mirror struct a plain vec4 pair — the shader
// still only reads .rgb, exactly like GL).
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

layout(std140, set = 0, binding = 0) uniform BackgroundUBO {
    vec4 uTopColor;
    vec4 uBottomColor;
};

void main() {
    // Smooth gradient from bottom to top
    float t = vUV.y;
    t = t * t * (3.0 - 2.0 * t); // smoothstep-like curve
    FragColor = vec4(mix(uBottomColor.rgb, uTopColor.rgb, t), 1.0);
}
