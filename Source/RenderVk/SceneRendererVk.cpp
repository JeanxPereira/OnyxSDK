#include <Onyx/RenderVk/SceneRendererVk.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cmath>
#include <cstring>

namespace Onyx::RenderVk {

using Onyx::Domain::GpuVertex;
using Onyx::Parsers::BlendMode;
using Onyx::Parsers::MaterialDesc;
using Onyx::Parsers::MeshPart;
using Onyx::Parsers::ObjectData;
using Onyx::Parsers::SceneData;
using Onyx::Parsers::TextureRole;
using Onyx::Rendering::RenderBatch;
using Onyx::Rendering::ShadingMode;

namespace {

// scene.vert/scene.frag keep GL's uShadingMode integer values (divergence 2
// in scene.frag's top comment: Matcap==1 is never sent, and any value other
// than 2 falls through to Solid). GL's own Wireframe/TexturedWire handling
// (SceneRenderer::Render) maps Wireframe -> Matcap's filled-face pass and
// TexturedWire -> Textured's filled-face pass before drawing the base
// geometry, then draws a SEPARATE GL_LINE overlay pass this Vulkan port
// drops entirely (scene.frag divergence 4 -- no wireframe pipeline exists).
// So the base-geometry shading value this port sends is: Solid/Wireframe ->
// 0 (Wireframe's overlay-less base was already Matcap in GL, which itself
// aliases to Solid's shader path here since Matcap has no Vulkan path
// either); Textured/TexturedWire -> 2.
int ShadingModeInt(ShadingMode mode) {
    switch (mode) {
        case ShadingMode::Textured:
        case ShadingMode::TexturedWire:
            return 2;
        case ShadingMode::Solid:
        case ShadingMode::Matcap:
        case ShadingMode::Wireframe:
        default:
            return 0;
    }
}

// ── Idle-pose joint math -- exact port of Source/Rendering/SceneRenderer.cpp's
// BuildLocalTRS (see that file for the full derivation comment; reproduced
// here verbatim, math untouched, because SceneRendererVk cannot link
// Onyx_Render to reuse the GL translation unit itself). ─────────────────
glm::mat4 BuildLocalTRS(const ObjectData& obj, int i) {
    const auto& v4 = obj.vectors4[i];  // local translation
    const auto& v5 = obj.vectors5[i];  // rotation (Q.14 fixed-point)
    const auto& v6 = obj.vectors6[i];  // local scale

    const float Q14 = 1.0f / (1 << 14);

    glm::mat4 S = glm::scale(glm::mat4(1.0f),
                             glm::vec3(v6.x != 0.0f ? v6.x : 1.0f,
                                       v6.y != 0.0f ? v6.y : 1.0f,
                                       v6.z != 0.0f ? v6.z : 1.0f));

    glm::quat rot;
    const bool isQuat = obj.joints[i].isQuaternion;

    if (isQuat) {
        float qx = float(v5.x) * Q14;
        float qy = float(v5.y) * Q14;
        float qz = float(v5.z) * Q14;
        float qw = float(v5.w) * Q14;
        float qlen = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
        if (qlen > 0.0001f) { qx /= qlen; qy /= qlen; qz /= qlen; qw /= qlen; }
        else                 { qx = 0; qy = 0; qz = 0; qw = 1; }
        rot = glm::quat(qw, qx, qy, qz);
    } else {
        const float halfToRad = (0.5f * glm::pi<float>()) / 180.0f;
        float ex = float(v5.x) * Q14 * 360.0f * halfToRad;
        float ey = float(v5.y) * Q14 * 360.0f * halfToRad;
        float ez = float(v5.z) * Q14 * 360.0f * halfToRad;
        float sx = std::sin(ex), cx = std::cos(ex);
        float sy = std::sin(ey), cy = std::cos(ey);
        float sz = std::sin(ez), cz = std::cos(ez);
        float qx = sx * cy * cz - cx * sy * sz;
        float qy = cx * sy * cz + sx * cy * sz;
        float qz = cx * cy * sz - sx * sy * cz;
        float qw = cx * cy * cz + sx * sy * sz;
        float qlen = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
        if (qlen > 0.0001f) { qx /= qlen; qy /= qlen; qz /= qlen; qw /= qlen; }
        else                 { qx = 0; qy = 0; qz = 0; qw = 1; }
        rot = glm::quat(qw, qx, qy, qz);
    }

    glm::mat4 R = glm::mat4_cast(rot);
    glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(v4.x, v4.y, v4.z));
    return T * R * S;
}

// Persistently-mapped-buffer write helper -- every CPU_TO_GPU buffer this
// file creates is mapped once at creation (Resources::CreateBuffer already
// requests VMA_ALLOCATION_CREATE_MAPPED_BIT for that memory usage), so
// updating its contents on a later frame is just a memcpy into the cached
// pointer, no vmaGetAllocationInfo/ctx needed again.
void WriteMapped(void* mapped, const void* data, size_t size) {
    if (mapped) std::memcpy(mapped, data, size);
}

} // namespace

// ── Idle-pose joint palette ──────────────────────────────────────────────

void SceneRendererVk::ComputeJointPalette(const ObjectData& skeleton) {
    const size_t n = skeleton.joints.size();
    m_jointPalette.assign(n, glm::mat4(1.0f));

    std::vector<glm::mat4> globalMats(n, glm::mat4(1.0f));
    for (size_t i = 0; i < n; ++i) {
        const auto& j = skeleton.joints[i];
        glm::mat4 local = BuildLocalTRS(skeleton, static_cast<int>(i));
        if (j.parent >= 0 && static_cast<size_t>(j.parent) < n) {
            globalMats[i] = globalMats[static_cast<size_t>(j.parent)] * local;
        } else {
            globalMats[i] = local;
        }
        m_jointPalette[i] = globalMats[i] * j.bindToJointMat;
    }
}

std::vector<glm::mat4> SceneRendererVk::BuildBatchPalette(const std::vector<uint16_t>& jointMap) const {
    if (jointMap.empty() || m_jointPalette.empty()) return {glm::mat4(1.0f)};

    std::vector<glm::mat4> remapped(jointMap.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < jointMap.size(); ++i) {
        uint16_t globalIdx = jointMap[i];
        if (globalIdx < m_jointPalette.size()) remapped[i] = m_jointPalette[globalIdx];
    }
    return remapped;
}

// ── Build ─────────────────────────────────────────────────────────────────

bool SceneRendererVk::Build(VkContext& ctx, const ScenePipelines& pipelines, const SceneData& scene,
                            std::string& err) {
    Clear(ctx);
    m_pipelines = &pipelines;

    // GOW2 models face -Z, GOWR is already screen-correct -- identical to
    // GL's SceneRenderer::Build.
    m_instanceTransform = scene.flipZ
        ? glm::scale(scene.instanceTransform, glm::vec3(1.0f, 1.0f, -1.0f))
        : scene.instanceTransform;

    // ── shared sampler + 1x1 white default (absent-role fallback) ──────
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f; // Resources::CreateImage2D fixes mipLevels=1
    VkResult vr = vkCreateSampler(ctx.Device(), &samplerInfo, nullptr, &m_sampler);
    if (vr != VK_SUCCESS) {
        err = "SceneRendererVk::Build: vkCreateSampler failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        Clear(ctx);
        return false;
    }

    const uint8_t white[4] = {255, 255, 255, 255};
    m_defaultTex = Resources::CreateImage2D(ctx, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                            VK_SAMPLE_COUNT_1_BIT, err);
    if (m_defaultTex.img == VK_NULL_HANDLE || !Resources::UploadImage(ctx, m_defaultTex, white, err)) {
        err = "SceneRendererVk::Build: default texture: " + err;
        Clear(ctx);
        return false;
    }

    // ── flat texture pool, index-aligned with scene.textures ───────────
    m_textures.resize(scene.textures.size());
    for (size_t i = 0; i < scene.textures.size(); ++i) {
        if (!scene.textures[i] || !scene.textures[i]->IsValid()) continue;
        const auto& tex = *scene.textures[i];
        Image2D img = Resources::CreateImage2D(ctx, tex.width, tex.height, VK_FORMAT_R8G8B8A8_UNORM,
                                                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                                VK_SAMPLE_COUNT_1_BIT, err);
        if (img.img == VK_NULL_HANDLE || !Resources::UploadImage(ctx, img, tex.pixels.data(), err)) {
            err = "SceneRendererVk::Build: texture[" + std::to_string(i) + "] '" + tex.name +
                  "': " + err;
            Resources::Destroy(ctx, img);
            Clear(ctx);
            return false;
        }
        m_textures[i] = img;
    }

    // ── idle-pose joint palette (rest pose, no animation) ───────────────
    if (scene.skeleton) ComputeJointPalette(*scene.skeleton);

    // ── one shared descriptor pool: 2 frame sets + up to one set per
    // mesh part (an upper bound -- some parts may be skipped below for
    // empty vertices/indices, same as GL). ──────────────────────────────
    const uint32_t batchCap = static_cast<uint32_t>(scene.meshParts.size());
    std::vector<VkDescriptorPoolSize> poolSizes;
    poolSizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 + batchCap});
    if (batchCap > 0) {
        poolSizes.push_back(
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, batchCap * static_cast<uint32_t>(SceneRole::kCount)});
        poolSizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, batchCap});
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 2 + batchCap;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    vr = vkCreateDescriptorPool(ctx.Device(), &poolInfo, nullptr, &m_descriptorPool);
    if (vr != VK_SUCCESS) {
        err = "SceneRendererVk::Build: vkCreateDescriptorPool failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        Clear(ctx);
        return false;
    }

    // ── frame sets: main view + rotation-only sky view ──────────────────
    {
        std::array<VkDescriptorSetLayout, 2> layouts{pipelines.frameSetLayout, pipelines.frameSetLayout};
        std::array<VkDescriptorSet, 2> sets{};
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = m_descriptorPool;
        ai.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        ai.pSetLayouts = layouts.data();
        vr = vkAllocateDescriptorSets(ctx.Device(), &ai, sets.data());
        if (vr != VK_SUCCESS) {
            err = "SceneRendererVk::Build: vkAllocateDescriptorSets (frame) failed (VkResult " +
                  std::to_string(static_cast<int>(vr)) + ")";
            Clear(ctx);
            return false;
        }
        m_frameSetMain = sets[0];
        m_frameSetSky = sets[1];

        m_frameBufMain = Resources::CreateBuffer(ctx, sizeof(SceneFrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                  VMA_MEMORY_USAGE_CPU_TO_GPU, err);
        m_frameBufSky = (m_frameBufMain.buf != VK_NULL_HANDLE)
                            ? Resources::CreateBuffer(ctx, sizeof(SceneFrameUBO),
                                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                       VMA_MEMORY_USAGE_CPU_TO_GPU, err)
                            : Buffer{};
        if (m_frameBufMain.buf == VK_NULL_HANDLE || m_frameBufSky.buf == VK_NULL_HANDLE) {
            err = "SceneRendererVk::Build: frame UBO buffer: " + err;
            Clear(ctx);
            return false;
        }

        VmaAllocationInfo info{};
        vmaGetAllocationInfo(ctx.Allocator(), m_frameBufMain.alloc, &info);
        m_frameBufMainMapped = info.pMappedData;
        vmaGetAllocationInfo(ctx.Allocator(), m_frameBufSky.alloc, &info);
        m_frameBufSkyMapped = info.pMappedData;
        if (!m_frameBufMainMapped || !m_frameBufSkyMapped) {
            err = "SceneRendererVk::Build: frame UBO buffer is not host-mapped";
            Clear(ctx);
            return false;
        }

        auto writeFrameSet = [&](VkDescriptorSet set, const Buffer& buf) {
            VkDescriptorBufferInfo bi{buf.buf, 0, sizeof(SceneFrameUBO)};
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = set;
            w.dstBinding = 0;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w.pBufferInfo = &bi;
            vkUpdateDescriptorSets(ctx.Device(), 1, &w, 0, nullptr);
        };
        writeFrameSet(m_frameSetMain, m_frameBufMain);
        writeFrameSet(m_frameSetSky, m_frameBufSky);
    }

    // ── batches, in scene.meshParts order (GetBatches() order the report
    // depends on matching GL exactly) ────────────────────────────────────
    m_batches.reserve(scene.meshParts.size());
    m_gpuBatches.reserve(scene.meshParts.size());
    for (const auto& part : scene.meshParts) {
        if (part.vertices.empty() || part.indices.empty()) continue;
        if (!BuildBatch(ctx, pipelines, scene, part, err)) {
            Clear(ctx);
            return false;
        }
    }

    // ── bucket classification -- mirrors GL's Build() tail exactly
    // (SceneRenderer.cpp:172-180): sky first, then "additive" (blendMode
    // != Normal OR textureLayer > 0), everything else opaque. ───────────
    for (size_t i = 0; i < m_batches.size(); ++i) {
        const RenderBatch& b = m_batches[i];
        if (b.isSky) {
            m_skyIdx.push_back(i);
        } else if (b.blendMode != BlendMode::Normal || b.textureLayer > 0) {
            m_additiveIdx.push_back(i);
        } else {
            m_opaqueIdx.push_back(i);
        }
    }

    return true;
}

bool SceneRendererVk::BuildBatch(VkContext& ctx, const ScenePipelines& pipelines, const SceneData& scene,
                                  const MeshPart& part, std::string& err) {
    RenderBatch batch;
    batch.name = part.name;
    batch.textureLayer = part.textureLayer;
    batch.jointMap = part.jointMap;
    batch.hasSkeleton = scene.HasSkeleton();
    batch.isSky = part.isSky;
    batch.meshHash = part.meshHash;
    batch.vertexCount = static_cast<int>(part.vertices.size());
    batch.triangleCount = static_cast<int>(part.indices.size()) / 3;

    GpuBatch gb;
    gb.vertexBuf = Resources::CreateBuffer(ctx, sizeof(GpuVertex) * part.vertices.size(),
                                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                           VMA_MEMORY_USAGE_GPU_ONLY, err);
    if (gb.vertexBuf.buf == VK_NULL_HANDLE ||
        !Resources::Upload(ctx, gb.vertexBuf, part.vertices.data(), gb.vertexBuf.size, err)) {
        err = "batch '" + part.name + "' vertex buffer: " + err;
        Resources::Destroy(ctx, gb.vertexBuf);
        return false;
    }

    gb.indexBuf = Resources::CreateBuffer(ctx, sizeof(uint32_t) * part.indices.size(),
                                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                          VMA_MEMORY_USAGE_GPU_ONLY, err);
    if (gb.indexBuf.buf == VK_NULL_HANDLE ||
        !Resources::Upload(ctx, gb.indexBuf, part.indices.data(), gb.indexBuf.size, err)) {
        err = "batch '" + part.name + "' index buffer: " + err;
        Resources::Destroy(ctx, gb.vertexBuf);
        Resources::Destroy(ctx, gb.indexBuf);
        return false;
    }
    gb.indexCount = static_cast<uint32_t>(part.indices.size());

    // ── material -- unconditional role lookups, no positional layer
    // index, matching GL's Build() exactly (SceneRenderer.cpp:107-134). ──
    const Image2D* diffuseImg = nullptr;
    const Image2D* normalImg = nullptr;
    const Image2D* aoImg = nullptr;
    const Image2D* glossImg = nullptr;
    const Image2D* envImg = nullptr;
    const Image2D* scatterImg = nullptr;

    if (part.materialId < scene.materials.size()) {
        const MaterialDesc& mat = scene.materials[part.materialId];
        std::memcpy(batch.materialColor, mat.baseColor, sizeof(float) * 4);
        std::memcpy(batch.layerColor, mat.blendColor, sizeof(float) * 4);
        batch.blendMode = mat.blendMode;
        batch.uvOffset[0] = mat.uvOffset[0];
        batch.uvOffset[1] = mat.uvOffset[1];
        batch.metallic = mat.metallic;

        // Reused verbatim per the brief -- pure, public, no reimplementation.
        std::array<int, 9> roles = Rendering::SceneRenderer::ResolveRoleIndices(mat);
        auto imgFor = [&](TextureRole role) -> const Image2D* {
            int idx = roles[static_cast<size_t>(role)];
            if (idx >= 0 && static_cast<size_t>(idx) < m_textures.size() &&
                m_textures[static_cast<size_t>(idx)].img != VK_NULL_HANDLE) {
                return &m_textures[static_cast<size_t>(idx)];
            }
            return nullptr;
        };
        diffuseImg = imgFor(TextureRole::Diffuse);
        normalImg = imgFor(TextureRole::Normal);
        aoImg = imgFor(TextureRole::Occlusion);
        glossImg = imgFor(TextureRole::Gloss);
        envImg = imgFor(TextureRole::EnvMap);
        scatterImg = imgFor(TextureRole::Scatter);

        // Sentinel semantics only -- see the RenderBatch-reuse note at the
        // top of SceneRendererVk.h. Height/Detail/Emissive are resolved by
        // ResolveRoleIndices but never sampled (scene.frag has no slot for
        // them either, matching GL exactly), so left unset here too.
        batch.hasTexture = diffuseImg != nullptr;
        batch.hasEnvmap = envImg != nullptr;
        batch.texture0 = diffuseImg ? 1u : 0u;
        batch.texture1 = envImg ? 1u : 0u;
        batch.texNormal = normalImg ? 1u : 0u;
        batch.texAO = aoImg ? 1u : 0u;
        batch.texGloss = glossImg ? 1u : 0u;
        batch.texScatter = scatterImg ? 1u : 0u;
    }

    const bool useJoints = batch.hasSkeleton && !batch.jointMap.empty();

    SceneMaterialUBO mubo{};
    mubo.baseColor = glm::vec4(batch.materialColor[0], batch.materialColor[1], batch.materialColor[2],
                               batch.materialColor[3]);
    mubo.layerColor = glm::vec4(batch.layerColor[0], batch.layerColor[1], batch.layerColor[2],
                                batch.layerColor[3]);
    mubo.uvOffset = glm::vec2(batch.uvOffset[0], batch.uvOffset[1]);
    mubo.metallic = batch.metallic;
    uint32_t flags = 0;
    if (batch.hasTexture) flags |= SceneFlags::kUseTexture;
    if (normalImg) flags |= SceneFlags::kHasNormal;
    if (aoImg) flags |= SceneFlags::kHasAO;
    if (glossImg) flags |= SceneFlags::kHasGloss;
    if (scatterImg) flags |= SceneFlags::kHasScatter;
    if (useJoints) flags |= SceneFlags::kUseJoints;
    if (batch.hasEnvmap) flags |= SceneFlags::kHasEnvmap;
    mubo.flags = flags;

    gb.materialUbo = Resources::CreateBuffer(ctx, sizeof(SceneMaterialUBO),
                                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             VMA_MEMORY_USAGE_GPU_ONLY, err);
    if (gb.materialUbo.buf == VK_NULL_HANDLE || !Resources::Upload(ctx, gb.materialUbo, &mubo, sizeof(mubo), err)) {
        err = "batch '" + part.name + "' material UBO: " + err;
        Resources::Destroy(ctx, gb.vertexBuf);
        Resources::Destroy(ctx, gb.indexBuf);
        Resources::Destroy(ctx, gb.materialUbo);
        return false;
    }

    // Bound even for unskinned batches, identity entry, per scene.vert's
    // comment -- a safety net FLAG_USE_JOINTS still gates whether it's read.
    std::vector<glm::mat4> palette = useJoints ? BuildBatchPalette(batch.jointMap)
                                                : std::vector<glm::mat4>{glm::mat4(1.0f)};
    if (palette.empty()) palette.push_back(glm::mat4(1.0f));

    gb.jointSsbo = Resources::CreateBuffer(ctx, sizeof(glm::mat4) * palette.size(),
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                           VMA_MEMORY_USAGE_GPU_ONLY, err);
    if (gb.jointSsbo.buf == VK_NULL_HANDLE ||
        !Resources::Upload(ctx, gb.jointSsbo, palette.data(), sizeof(glm::mat4) * palette.size(), err)) {
        err = "batch '" + part.name + "' joint SSBO: " + err;
        Resources::Destroy(ctx, gb.vertexBuf);
        Resources::Destroy(ctx, gb.indexBuf);
        Resources::Destroy(ctx, gb.materialUbo);
        Resources::Destroy(ctx, gb.jointSsbo);
        return false;
    }

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &pipelines.batchSetLayout;
    VkResult vr = vkAllocateDescriptorSets(ctx.Device(), &ai, &gb.set);
    if (vr != VK_SUCCESS) {
        err = "batch '" + part.name + "' vkAllocateDescriptorSets failed (VkResult " +
              std::to_string(static_cast<int>(vr)) + ")";
        Resources::Destroy(ctx, gb.vertexBuf);
        Resources::Destroy(ctx, gb.indexBuf);
        Resources::Destroy(ctx, gb.materialUbo);
        Resources::Destroy(ctx, gb.jointSsbo);
        return false;
    }

    VkDescriptorBufferInfo materialInfo{gb.materialUbo.buf, 0, sizeof(SceneMaterialUBO)};
    VkDescriptorBufferInfo ssboInfo{gb.jointSsbo.buf, 0, VK_WHOLE_SIZE};

    std::array<VkDescriptorImageInfo, SceneRole::kCount> imageInfos{};
    auto fillImg = [&](int slot, const Image2D* img) {
        imageInfos[static_cast<size_t>(slot)].sampler = m_sampler;
        imageInfos[static_cast<size_t>(slot)].imageView = img ? img->view : m_defaultTex.view;
        imageInfos[static_cast<size_t>(slot)].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    };
    fillImg(SceneRole::kDiffuse, diffuseImg);
    fillImg(SceneRole::kNormal, normalImg);
    fillImg(SceneRole::kAO, aoImg);
    fillImg(SceneRole::kGloss, glossImg);
    fillImg(SceneRole::kEnvMap, envImg);
    fillImg(SceneRole::kScatter, scatterImg);

    std::array<VkWriteDescriptorSet, 3> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = gb.set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &materialInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = gb.set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = static_cast<uint32_t>(SceneRole::kCount);
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = imageInfos.data();

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = gb.set;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].pBufferInfo = &ssboInfo;

    vkUpdateDescriptorSets(ctx.Device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    m_batches.push_back(std::move(batch));
    m_gpuBatches.push_back(std::move(gb));
    return true;
}

// ── Render ────────────────────────────────────────────────────────────────

void SceneRendererVk::DrawBatch(VkCommandBuffer cmd, size_t index) const {
    const GpuBatch& gb = m_gpuBatches[index];
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines->layout, 1, 1, &gb.set, 0, nullptr);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &gb.vertexBuf.buf, &offset);
    vkCmdBindIndexBuffer(cmd, gb.indexBuf.buf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, gb.indexCount, 1, 0, 0, 0);
}

void SceneRendererVk::Render(VkCommandBuffer cmd, const glm::mat4& view, const glm::mat4& proj,
                              ShadingMode mode, int viewportW, int viewportH) {
    if (m_batches.empty() || !m_pipelines) return;

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(viewportW);
    vp.height = static_cast<float>(viewportH);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<uint32_t>(viewportW), static_cast<uint32_t>(viewportH)};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    const int shadingInt = ShadingModeInt(mode);

    // Push constant model matrix is the same for every batch (GL sets
    // uModelTransform once per RenderBatches() call, not per batch) --
    // issue it once, before any draw; it stays valid across every pipeline
    // bound below since all four scene pipeline variants share one
    // VkPipelineLayout (Pipelines::CreateScene).
    vkCmdPushConstants(cmd, m_pipelines->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4),
                       &m_instanceTransform);

    // set 0 (main view) -- cameraPos fed EXACTLY glm::vec3(glm::inverse(view)[3])
    // per Pipelines.h's Camera convention note, not a caller-tracked eye position.
    SceneFrameUBO mainUbo{};
    mainUbo.view = view;
    mainUbo.proj = proj;
    mainUbo.cameraPos = glm::vec3(glm::inverse(view)[3]);
    mainUbo.shadingMode = shadingInt;
    WriteMapped(m_frameBufMainMapped, &mainUbo, sizeof(mainUbo));

    // Sky pass first (GL: RenderSky is called before Render() by the
    // viewport) -- rotation-only view, own frame UBO/set so the sky and
    // main CPU writes never race each other (see the field comment on
    // m_frameBufSky in the header). Unexercised by the M0 corpus (isSky is
    // always false there) but wired per the brief.
    if (!m_skyIdx.empty()) {
        glm::mat4 skyView = glm::mat4(glm::mat3(view));
        SceneFrameUBO skyUbo{};
        skyUbo.view = skyView;
        skyUbo.proj = proj;
        skyUbo.cameraPos = glm::vec3(glm::inverse(skyView)[3]);
        skyUbo.shadingMode = shadingInt;
        WriteMapped(m_frameBufSkyMapped, &skyUbo, sizeof(skyUbo));

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines->sky);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines->layout, 0, 1, &m_frameSetSky, 0,
                                nullptr);
        for (size_t idx : m_skyIdx) DrawBatch(cmd, idx);
    }

    // Opaque pass -- depth write on, blend off, exactly the batches GL's
    // classification (Build()'s tail) would never blend regardless of a
    // material's own alpha (translucent-looking Normal-blend batches with
    // textureLayer==0 land here too -- ported faithfully, not "fixed").
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines->opaque);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines->layout, 0, 1, &m_frameSetMain, 0,
                            nullptr);
    for (size_t idx : m_opaqueIdx) DrawBatch(cmd, idx);

    // Additive/blended pass -- per-batch pipeline selection mirrors GL's
    // RenderBatches blend-func switch exactly (SceneRenderer.cpp:654-658):
    // binary Additive-vs-not; Subtractive/EnvMap fall through to the
    // Normal-blend pipeline (GL gives them no distinct blend func either).
    // set 0 is still m_frameSetMain from the opaque pass above -- binding a
    // different pipeline does not disturb an already-bound descriptor set.
    for (size_t idx : m_additiveIdx) {
        VkPipeline pipe = (m_batches[idx].blendMode == BlendMode::Additive) ? m_pipelines->blendAdditive
                                                                            : m_pipelines->blendNormal;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        DrawBatch(cmd, idx);
    }
}

// ── RenderBackground ─────────────────────────────────────────────────────

bool SceneRendererVk::RenderBackground(VkContext& ctx, const BackgroundPipeline& pipeline, VkCommandBuffer cmd,
                                       const glm::vec3& topColor, const glm::vec3& bottomColor,
                                       std::string& err) {
    if (m_bgSet == VK_NULL_HANDLE) {
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &size;
        VkResult vr = vkCreateDescriptorPool(ctx.Device(), &poolInfo, nullptr, &m_bgPool);
        if (vr != VK_SUCCESS) {
            err = "SceneRendererVk::RenderBackground: vkCreateDescriptorPool failed (VkResult " +
                  std::to_string(static_cast<int>(vr)) + ")";
            return false;
        }

        m_bgBuf = Resources::CreateBuffer(ctx, sizeof(BackgroundUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                          VMA_MEMORY_USAGE_CPU_TO_GPU, err);
        if (m_bgBuf.buf == VK_NULL_HANDLE) {
            err = "SceneRendererVk::RenderBackground: UBO buffer: " + err;
            return false;
        }
        VmaAllocationInfo info{};
        vmaGetAllocationInfo(ctx.Allocator(), m_bgBuf.alloc, &info);
        m_bgBufMapped = info.pMappedData;
        if (!m_bgBufMapped) {
            err = "SceneRendererVk::RenderBackground: UBO buffer is not host-mapped";
            return false;
        }

        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = m_bgPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &pipeline.setLayout;
        vr = vkAllocateDescriptorSets(ctx.Device(), &ai, &m_bgSet);
        if (vr != VK_SUCCESS) {
            err = "SceneRendererVk::RenderBackground: vkAllocateDescriptorSets failed (VkResult " +
                  std::to_string(static_cast<int>(vr)) + ")";
            return false;
        }

        VkDescriptorBufferInfo bi{m_bgBuf.buf, 0, sizeof(BackgroundUBO)};
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = m_bgSet;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &bi;
        vkUpdateDescriptorSets(ctx.Device(), 1, &w, 0, nullptr);
    }

    BackgroundUBO ubo{};
    ubo.topColor = glm::vec4(topColor, 1.0f);
    ubo.bottomColor = glm::vec4(bottomColor, 1.0f);
    WriteMapped(m_bgBufMapped, &ubo, sizeof(ubo));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1, &m_bgSet, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    return true;
}

// ── Clear ─────────────────────────────────────────────────────────────────

void SceneRendererVk::Clear(VkContext& ctx) {
    for (auto& gb : m_gpuBatches) {
        Resources::Destroy(ctx, gb.vertexBuf);
        Resources::Destroy(ctx, gb.indexBuf);
        Resources::Destroy(ctx, gb.materialUbo);
        Resources::Destroy(ctx, gb.jointSsbo);
        // gb.set is freed implicitly when m_descriptorPool is destroyed below
        // (the pool was not created with FREE_DESCRIPTOR_SET_BIT).
    }
    m_gpuBatches.clear();
    m_batches.clear();
    m_opaqueIdx.clear();
    m_additiveIdx.clear();
    m_skyIdx.clear();
    m_jointPalette.clear();

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx.Device(), m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    m_frameSetMain = VK_NULL_HANDLE;
    m_frameSetSky = VK_NULL_HANDLE;
    Resources::Destroy(ctx, m_frameBufMain);
    Resources::Destroy(ctx, m_frameBufSky);
    m_frameBufMainMapped = nullptr;
    m_frameBufSkyMapped = nullptr;

    for (auto& img : m_textures) Resources::Destroy(ctx, img);
    m_textures.clear();
    Resources::Destroy(ctx, m_defaultTex);
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(ctx.Device(), m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }

    if (m_bgPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx.Device(), m_bgPool, nullptr);
        m_bgPool = VK_NULL_HANDLE;
    }
    m_bgSet = VK_NULL_HANDLE;
    Resources::Destroy(ctx, m_bgBuf);
    m_bgBufMapped = nullptr;

    m_pipelines = nullptr;
    m_instanceTransform = glm::mat4(1.0f);
}

} // namespace Onyx::RenderVk
