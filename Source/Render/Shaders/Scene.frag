#version 450

layout(location = 0) in vec3  vWorldPos;
layout(location = 1) in vec3  vWorldNormal;
layout(location = 2) in vec3  vViewNormal;
layout(location = 3) in vec3  vViewPos;
layout(location = 4) in vec2  vUV;
layout(location = 5) in vec2  vUV1;
layout(location = 6) in vec4  vColor;
layout(location = 7) in float vDet;
layout(location = 8) in vec3  vWorldTangent;
layout(location = 9) in float vHandedness;

layout(std140, set = 0, binding = 0) uniform FrameUBO {
    mat4 uView;
    mat4 uProjection;
    vec3 uCameraPos;
    int  uShadingMode;
};

layout(std140, set = 1, binding = 0) uniform MaterialUBO {
    vec4  uBaseColor;
    vec4  uLayerColor;
    vec2  uUvOffset;
    float uMetallic;
    uint  uFlags;
    vec4  uNormalDecode;
};

const uint FLAG_USE_TEXTURE = 1u << 0;
const uint FLAG_HAS_NORMAL  = 1u << 1;
const uint FLAG_HAS_AO      = 1u << 2;
const uint FLAG_HAS_GLOSS   = 1u << 3;
const uint FLAG_HAS_SCATTER = 1u << 4;
const uint FLAG_HAS_ENVMAP  = 1u << 6;
const uint FLAG_HAS_OPACITY = 1u << 7;
const uint FLAG_NORMAL_XYZ  = 1u << 8;

layout(set = 1, binding = 1) uniform sampler2D uRoleTex[7];
const int ROLE_DIFFUSE = 0;
const int ROLE_NORMAL  = 1;
const int ROLE_AO      = 2;
const int ROLE_GLOSS   = 3;
const int ROLE_ENVMAP  = 4;
const int ROLE_SCATTER = 5;
const int ROLE_OPACITY = 6;

layout(location = 0) out vec4 FragColor;

const vec3  kLightDir       = vec3(0.4, 0.8, 0.5);
vec3 ApplyNormalMap(vec3 N) {
    vec3 T = vWorldTangent - N * dot(N, vWorldTangent);
    float len = length(T);
    if ((uFlags & FLAG_HAS_NORMAL) == 0u || len < 1e-5) return N;
    T /= len;
    vec3 B = cross(N, T) * vHandedness * sign(vDet);

    vec3 tangentNormal = texture(uRoleTex[ROLE_NORMAL], vUV).rgb
                       * uNormalDecode.x + uNormalDecode.y;
    tangentNormal.y *= uNormalDecode.w;
    tangentNormal.xy *= uNormalDecode.z;

    if ((uFlags & FLAG_NORMAL_XYZ) == 0u) {
        tangentNormal.z = sqrt(max(1.0 - dot(tangentNormal.xy, tangentNormal.xy), 0.0));
    }

    float normalLength = length(tangentNormal);
    if (normalLength < 1e-5) return N;
    tangentNormal /= normalLength;
    return normalize(T * tangentNormal.x + B * tangentNormal.y + N * tangentNormal.z);
}

float DistributionGGX(float ndoth, float rough) {
    float a  = rough * rough;
    float a2 = a * a;
    float d  = ndoth * ndoth * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * d * d, 1e-7);
}

float GeometrySmith(float ndotv, float ndotl, float rough) {
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    float gv = ndotv / (ndotv * (1.0 - k) + k);
    float gl = ndotl / (ndotl * (1.0 - k) + k);
    return gv * gl;
}

vec3 FresnelSchlick(float ct, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - ct, 0.0, 1.0), 5.0);
}

void main() {
    vec3 N = normalize(vWorldNormal);

    vec4 clr = vec4(1.0);

    if ((uFlags & FLAG_USE_TEXTURE) != 0u) {
        clr = texture(uRoleTex[ROLE_DIFFUSE], vUV);
    }

    if ((uFlags & FLAG_HAS_ENVMAP) != 0u) {
        vec3 envColor = texture(uRoleTex[ROLE_ENVMAP], vUV1).rgb;
        clr.rgb = clr.rgb * (1.0 - clr.a) + envColor * clr.a;
        clr.a = 1.0;
    }

    clr *= vColor;

    clr *= uBaseColor * uLayerColor;

    if ((uFlags & FLAG_HAS_OPACITY) != 0u) {
        clr.a *= texture(uRoleTex[ROLE_OPACITY], vUV).r;
        if (clr.a < 0.5) discard;
    }

    if (clr.a < 0.01) discard;

    if (uShadingMode == 2) {
        vec3 Nm = ApplyNormalMap(N);
        vec3 V  = normalize(uCameraPos - vWorldPos);
        vec3 L  = normalize(kLightDir);
        vec3 H  = normalize(L + V);

        float gloss = (uFlags & FLAG_HAS_GLOSS) != 0u ? texture(uRoleTex[ROLE_GLOSS], vUV).r : 0.35;
        float rough = clamp(1.0 - gloss, 0.045, 1.0);
        float ao    = (uFlags & FLAG_HAS_AO) != 0u ? texture(uRoleTex[ROLE_AO], vUV).r : 1.0;

        vec3 albedo = pow(clr.rgb, vec3(2.2));
        vec3 F0     = mix(vec3(0.04), albedo, uMetallic);
        vec3 kd     = albedo * (1.0 - uMetallic);

        float ndotv = max(dot(Nm, V), 1e-4);
        float ndotl = max(dot(Nm, L), 0.0);
        float ndoth = max(dot(Nm, H), 0.0);

        vec3  F    = FresnelSchlick(max(dot(H, V), 0.0), F0);
        float D    = DistributionGGX(ndoth, rough);
        float G    = GeometrySmith(ndotv, ndotl, rough);
        vec3  spec = (D * G * F) / max(4.0 * ndotv * ndotl, 1e-4);

        vec3 diffuse = kd / 3.14159265;

        if ((uFlags & FLAG_HAS_SCATTER) != 0u) {
            vec3 sss = texture(uRoleTex[ROLE_SCATTER], vUV).rgb;
            float wrapped = max(dot(Nm, L) * 0.5 + 0.5, 0.0);
            diffuse += kd * sss * wrapped * 0.35 / 3.14159265;
        }

        vec3 keyLight = vec3(3.0);
        vec3 color    = (diffuse + spec) * keyLight * ndotl;

        vec3 skyColor    = vec3(0.16, 0.18, 0.22);
        vec3 groundColor = vec3(0.06, 0.05, 0.05);
        vec3 ambient     = mix(groundColor, skyColor, Nm.y * 0.5 + 0.5);
        vec3 ambSpec     = FresnelSchlick(ndotv, F0) * skyColor * (1.0 - rough);
        color += (kd * ambient + ambSpec) * ao;

        color = color / (color + vec3(1.0));
        color = pow(color, vec3(1.0 / 2.2));

        FragColor = vec4(color, clr.a);
        return;
    }

    vec3 L = normalize(kLightDir);
    vec3 V = normalize(uCameraPos - vWorldPos);

    float diff = max(dot(N, L), 0.0);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0);

    vec3 fillDir = normalize(vec3(-0.5, 0.3, -0.5));
    float fillDiff = max(dot(N, fillDir), 0.0);

    float rim = 1.0 - max(dot(N, V), 0.0);
    rim = pow(rim, 3.0) * 0.15;

    vec3 ambient  = vec3(0.12);
    vec3 keyLight = vec3(1.0) * diff;
    vec3 specular = vec3(0.3) * spec;
    vec3 fill     = vec3(0.08, 0.10, 0.14) * fillDiff;

    vec3 lighting = ambient + keyLight + specular + fill + vec3(rim);
    FragColor = vec4(clr.rgb * lighting, clr.a);
}
