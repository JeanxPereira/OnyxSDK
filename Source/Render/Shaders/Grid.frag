#version 450

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

    vec4 nearPt = uInvViewProj * vec4(ndc, 0.0, 1.0);
    vec4 farPt  = uInvViewProj * vec4(ndc, 1.0, 1.0);

    vec3 rayOrigin = nearPt.xyz / nearPt.w;
    vec3 rayEnd    = farPt.xyz / farPt.w;
    vec3 rayDir    = normalize(rayEnd - rayOrigin);

    if (abs(rayDir.y) < 0.0001) discard;

    float t = -rayOrigin.y / rayDir.y;
    if (t < 0.0) discard;

    vec3 worldPos = rayOrigin + t * rayDir;

    vec4 clipPos = uViewProj * vec4(worldPos, 1.0);
    float ndcZ = clipPos.z / clipPos.w;
    if (ndcZ < 0.0 || ndcZ > 1.0) discard;
    gl_FragDepth = ndcZ;

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
