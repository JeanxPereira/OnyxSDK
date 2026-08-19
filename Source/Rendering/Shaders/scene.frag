#version 450

// ═══════════════════════════════════════════════════════════════════════
// Intentional divergences from the GL source this file ports
// (Source/Rendering/ShaderManager.cpp: SCENE_FRAG at :131, the
// uShadingMode==2 Solid/Textured gate at :268 — the M0 oracle corpus pins
// both sides of it). Everything NOT listed here — the lighting math, the
// role texture sampling, uMetallic's use, the Fresnel/GGX/Smith functions,
// the tangent-space normal-map reconstruction, the subsurface-scatter
// wrap term, the Reinhard tonemap + gamma pair, the Solid-mode
// three-point lighting — is a line-for-line port. Read this block before
// touching the file.
//
//  1. Uniforms -> descriptor sets (mechanical, brief-fixed, no math
//     changed). set 0 = per-frame UBO (view, proj, camera pos, mode);
//     set 1 = per-batch material UBO (baseColor, layerColor, uvOffset,
//     metallic, flags) + a fixed 6-slot combined-sampler role array +
//     the skinning SSBO (scene.vert). Push constant = per-draw model
//     matrix (64B).
//  2. Matcap mode (GL uShadingMode==1, a `uMatcap` sampler + the
//     Blender-matcap-UV branch) is DROPPED. It is an editor-only viewport
//     visualization aid with no bearing on asset-format fidelity, the
//     descriptor scheme above budgets no slot for it, and the oracle
//     corpus only pins Solid (0) and Textured (2). uShadingMode keeps its
//     GL integer values; 1 is simply never sent by the Vulkan path. The
//     `main()` branch below only special-cases `uShadingMode == 2`; any
//     other value (0, or 1 if it were ever sent) falls through to Solid
//     mode — an explicit, intentional fallthrough, not an oversight; see
//     the comment at the branch site.
//  3. Environment-map blending is a FAITHFUL PORT, not a divergence — an
//     earlier draft of this file wrongly dropped it under this heading,
//     reasoning it was an optional extra with no descriptor slot
//     budgeted; that was wrong on both counts: GL genuinely samples it
//     (Source/Rendering/ShaderManager.cpp:250-254) and the M0 oracle
//     corpus pins it on every sphere batch it renders. Corrected: role
//     slot 4 (documented in the first pass as "reserved/unused") is
//     `ROLE_ENVMAP`, fed by `FLAG_HAS_ENVMAP` in the material flags
//     (mirrors GL's `uUseEnvmap`/`batch->hasEnvmap`,
//     Source/Rendering/SceneRenderer.cpp:127/691). It samples `vUV1` (the
//     SECOND uv set, already threaded through scene.vert for exactly this
//     reason) and its placement matches GL exactly: after the diffuse
//     sample, before vertex-color modulation and the material/layer
//     tint, applying uniformly to BOTH Solid and Textured mode because it
//     sits entirely before the `uShadingMode` branch — see the code
//     below, not a paraphrase of it.
//  4. The wireframe debug overlay (GL uWireframeOverride/uWireColor: an
//     early-return solid-color branch paired with a second GL_LINE
//     polygon-mode pass in SceneRenderer::Render) is DROPPED. It is a
//     separate debug render pass wrapped around the base draw, not part
//     of the pinned lighting math, and the brief's material UBO has no
//     field for it. A later task can add a dedicated line-mode pipeline
//     if the overlay is wanted in Vulkan.
//  5. uLightDir, uNormalStrength and uUseVertexColor become compile-time
//     constants instead of uniforms/branches. All three are invariant at
//     every GL call site (Source/Rendering/SceneRenderer.cpp:633 sets
//     uLightDir to the same normalize(0.4,0.8,0.5) literal on every draw;
//     :637 sets uNormalStrength to the literal 1.0 every draw, so its
//     `nxy *= uNormalStrength` step was already a no-op; :679 sets
//     uUseVertexColor to 1 unconditionally every batch) — inlining them
//     changes nothing numerically and keeps them out of the brief's fixed
//     UBO shape, which lists none of the three.
//  6. GL's texture units (0 diffuse, 1 envmap, 2 matcap, 3 normal, 4 AO,
//     5 gloss, 6 scatter) are replaced by the fixed 6-slot role array
//     from the descriptor scheme: index 0 = diffuse, 1 = normal, 2 = AO
//     (GL's "Occlusion" role), 3 = gloss, 4 = envmap (divergence 3;
//     wrongly left unassigned in the first pass), 5 = scatter. This is
//     NOT a positional reindex of the loader's 9-entry
//     `Parsers::TextureRole` enum (that has Height at index 4, which GL
//     never binds to a texture unit or samples here at all) — it is
//     exactly the 6 textures GL's SCENE_FRAG actually samples today,
//     repacked onto a dense 0-5 array. See
//     `Onyx::Rendering::SceneRole`'s comment in Pipelines.h for the same
//     correction on the C++ side. No role's sampling math changed.
//  7. GL's top-level `uUseJoints` uniform becomes a bit (FLAG_USE_JOINTS)
//     in the per-batch MaterialUBO's `flags` field instead — mechanical
//     repacking onto the brief's fixed UBO shape; same branch, same
//     4-lane weighted sum, same wsum<0.001 degenerate guard (scene.vert).
//  8. gl_Position's clip-space Y (scene.vert) is produced identically to
//     GL — the Vulkan/GL clip-space Y difference is resolved once, in the
//     projection matrix itself (GLM_FORCE_DEPTH_ZERO_TO_ONE + a Y-flip
//     baked into the matrix, NOT a negative viewport or a shader-side
//     negation), per the plan's fixed camera convention documented in
//     Include/Onyx/RenderVk/Pipelines.h. Scoped claim, corrected from the
//     first pass: no SCENE or GRID shader performs a Y-flip itself (both
//     go through a projection matrix, so the fix above covers them).
//     background.vert/frag do NOT go through a projection matrix at all
//     (they write NDC directly from a procedural fullscreen-triangle UV)
//     and so are NOT covered by this fix — they needed, and got, their
//     own standalone correction; see background.frag's top comment and
//     Pipelines.h's "Camera convention" note for why grid needed none but
//     background did.
// ═══════════════════════════════════════════════════════════════════════

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
    int  uShadingMode;   // 0 = Solid, 2 = Textured (divergence 2)
};

layout(std140, set = 1, binding = 0) uniform MaterialUBO {
    vec4  uBaseColor;
    vec4  uLayerColor;
    vec2  uUvOffset;
    float uMetallic;
    uint  uFlags;
};

const uint FLAG_USE_TEXTURE = 1u << 0;
const uint FLAG_HAS_NORMAL  = 1u << 1;
const uint FLAG_HAS_AO      = 1u << 2;
const uint FLAG_HAS_GLOSS   = 1u << 3;
const uint FLAG_HAS_SCATTER = 1u << 4;
const uint FLAG_HAS_ENVMAP  = 1u << 6;   // divergence 3 — mirrors GL's uUseEnvmap

// Fixed 6-slot role array (divergence 6). Onyx::Rendering::SceneRole in
// Pipelines.h mirrors these indices for the C++ side.
layout(set = 1, binding = 1) uniform sampler2D uRoleTex[6];
const int ROLE_DIFFUSE = 0;
const int ROLE_NORMAL  = 1;
const int ROLE_AO      = 2;
const int ROLE_GLOSS   = 3;
const int ROLE_ENVMAP  = 4;
const int ROLE_SCATTER = 5;

layout(location = 0) out vec4 FragColor;

// Invariant at every GL call site — see divergence 5. GL normalizes this
// once on the CPU (Source/Rendering/SceneRenderer.cpp:633) and the shader
// normalizes it again on every use; kept unnormalized here for the same
// reason (matches the raw literal, `normalize()` at each use site below
// is idempotent on top of it).
const vec3  kLightDir       = vec3(0.4, 0.8, 0.5);
const float kNormalStrength = 1.0;

// Rebuilds the tangent frame and applies the normal map. The map's Z is
// reconstructed rather than sampled: the shipped normals are BC5, which
// stores only two channels, and reconstructing stays correct for the BC7
// ones too.
vec3 ApplyNormalMap(vec3 N) {
    vec3 T = vWorldTangent - N * dot(N, vWorldTangent);   // Gram-Schmidt
    float len = length(T);
    if ((uFlags & FLAG_HAS_NORMAL) == 0u || len < 1e-5) return N;
    T /= len;
    // A mirroring model transform flips the frame with it.
    vec3 B = cross(N, T) * vHandedness * sign(vDet);

    vec2 nxy = texture(uRoleTex[ROLE_NORMAL], vUV).rg * 2.0 - 1.0;
    nxy *= kNormalStrength;
    float nz = sqrt(max(1.0 - dot(nxy, nxy), 0.0));
    return normalize(T * nxy.x + B * nxy.y + N * nz);
}

// GGX / Trowbridge-Reitz
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

    // ── Build base color ─────────────────────────────────────────────
    vec4 clr = vec4(1.0);

    if ((uFlags & FLAG_USE_TEXTURE) != 0u) {
        clr = texture(uRoleTex[ROLE_DIFFUSE], vUV);
    }

    // Environment map blending (Go-style: lerp diffuse->envmap by diffuse
    // alpha) — ported faithfully, see divergence 3. Placement matches GL
    // exactly: after the diffuse sample, before vertex-color modulation
    // and the material/layer tint, and BEFORE the uShadingMode branch
    // below, so it applies uniformly to both Solid and Textured mode.
    if ((uFlags & FLAG_HAS_ENVMAP) != 0u) {
        vec3 envColor = texture(uRoleTex[ROLE_ENVMAP], vUV1).rgb;
        clr.rgb = clr.rgb * (1.0 - clr.a) + envColor * clr.a;
        clr.a = 1.0;
    }

    // Vertex color modulation — always on at every GL call site (divergence 5).
    clr *= vColor;

    // Material + layer tint
    clr *= uBaseColor * uLayerColor;

    // Alpha test
    if (clr.a < 0.01) discard;

    // ── Textured Mode: the material as the game authored it ──────────
    // Any uShadingMode other than 2 (including 0/Solid and 1/Matcap, the
    // latter never actually sent — divergence 2) falls through to Solid
    // mode below. Intentional, not an omission.
    if (uShadingMode == 2) {
        vec3 Nm = ApplyNormalMap(N);
        vec3 V  = normalize(uCameraPos - vWorldPos);
        vec3 L  = normalize(kLightDir);
        vec3 H  = normalize(L + V);

        // The maps are named for gloss, so roughness is its complement.
        float gloss = (uFlags & FLAG_HAS_GLOSS) != 0u ? texture(uRoleTex[ROLE_GLOSS], vUV).r : 0.35;
        float rough = clamp(1.0 - gloss, 0.045, 1.0);
        float ao    = (uFlags & FLAG_HAS_AO) != 0u ? texture(uRoleTex[ROLE_AO], vUV).r : 1.0;

        // The diffuse map is authored in display space; the lighting below
        // is linear, so it has to be decoded before it takes part in it.
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

        // Subsurface: the scatter map tints light that wraps past the
        // terminator, which is what keeps skin from reading as plastic.
        if ((uFlags & FLAG_HAS_SCATTER) != 0u) {
            vec3 sss = texture(uRoleTex[ROLE_SCATTER], vUV).rgb;
            float wrapped = max(dot(Nm, L) * 0.5 + 0.5, 0.0);
            diffuse += kd * sss * wrapped * 0.35 / 3.14159265;
        }

        vec3 keyLight = vec3(3.0);
        vec3 color    = (diffuse + spec) * keyLight * ndotl;

        // Two-band ambient standing in for an environment probe: a
        // hemisphere for the diffuse, a fresnel-weighted term for the
        // specular.
        vec3 skyColor    = vec3(0.16, 0.18, 0.22);
        vec3 groundColor = vec3(0.06, 0.05, 0.05);
        vec3 ambient     = mix(groundColor, skyColor, Nm.y * 0.5 + 0.5);
        vec3 ambSpec     = FresnelSchlick(ndotv, F0) * skyColor * (1.0 - rough);
        color += (kd * ambient + ambSpec) * ao;

        // Reinhard, then back to display space - the lighting above is linear.
        color = color / (color + vec3(1.0));
        color = pow(color, vec3(1.0 / 2.2));

        FragColor = vec4(color, clr.a);
        return;
    }

    // ── Solid Mode (Blinn-Phong with 3-point lighting) ───────────────
    vec3 L = normalize(kLightDir);
    vec3 V = normalize(uCameraPos - vWorldPos);

    // Key light
    float diff = max(dot(N, L), 0.0);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0);

    // Fill light (soft, from opposite side)
    vec3 fillDir = normalize(vec3(-0.5, 0.3, -0.5));
    float fillDiff = max(dot(N, fillDir), 0.0);

    // Rim light (fresnel-based)
    float rim = 1.0 - max(dot(N, V), 0.0);
    rim = pow(rim, 3.0) * 0.15;

    vec3 ambient  = vec3(0.12);
    vec3 keyLight = vec3(1.0) * diff;
    vec3 specular = vec3(0.3) * spec;
    vec3 fill     = vec3(0.08, 0.10, 0.14) * fillDiff;

    vec3 lighting = ambient + keyLight + specular + fill + vec3(rim);
    FragColor = vec4(clr.rgb * lighting, clr.a);
}
