#include "Onyx/Render/SceneRenderer.h"

#include <Onyx/Render/Skinning.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "ScenePipelines.h"
#include "ShaderTypes.h"

namespace Onyx::Render {

namespace {

using Services::Diag;
using Services::DiagSink;
using Services::Severity;

constexpr std::array<Domain::TextureRole, Shader::kRoleCount> kRoles{
    Domain::TextureRole::Diffuse, Domain::TextureRole::Normal,   Domain::TextureRole::Occlusion,
    Domain::TextureRole::Gloss,   Domain::TextureRole::EnvMap,   Domain::TextureRole::Scatter,
    Domain::TextureRole::Opacity,
};

constexpr std::array<uint32_t, Shader::kRoleCount> kRoleFlags{
    Shader::Flag::kUseTexture, Shader::Flag::kHasNormal,  Shader::Flag::kHasAO,     Shader::Flag::kHasGloss,
    Shader::Flag::kHasEnvmap,  Shader::Flag::kHasScatter, Shader::Flag::kHasOpacity,
};

constexpr const char* kRoleNames[Shader::kRoleCount] = {"Diffuse", "Normal", "Occlusion", "Gloss",
                                                        "EnvMap",  "Scatter", "Opacity"};

constexpr int32_t kUntried = -2;
constexpr int32_t kUseDefault = -1;

void Report(DiagSink* sink, Severity severity, const char* code, std::string message) {
    if (sink == nullptr) {
        return;
    }
    Diag diag;
    diag.severity = severity;
    diag.code = code;
    diag.message = std::move(message);
    sink->Report(std::move(diag));
}

// Must match the label ValidateSkinning puts at the start of its messages.
std::string PartLabel(const Domain::MeshPart& part, size_t index) {
    return part.name.empty() ? std::format("part {}", index) : std::format("part '{}'", part.name);
}

int16_t PackSnorm(float v) {
    if (!std::isfinite(v)) {
        v = 0.0f;
    }
    return static_cast<int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
}

uint8_t PackUnorm(float v) {
    if (!std::isfinite(v)) {
        v = 0.0f;
    }
    return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return std::isfinite(len) && len > 1e-12f ? v / len : fallback;
}

template <class T>
void AppendBytes(std::string& out, const T& value) {
    out.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

std::string MaterialKey(const Domain::Material& m, bool skinned) {
    std::string key;
    AppendBytes(key, m.baseColor);
    AppendBytes(key, m.tint);
    AppendBytes(key, m.blend);
    AppendBytes(key, m.uvOffset);
    AppendBytes(key, m.metallic);
    AppendBytes(key, m.normalEncoding);
    AppendBytes(key, m.vertexColorScale);
    AppendBytes(key, skinned);
    for (const auto& [role, index] : m.textures) {
        AppendBytes(key, role);
        AppendBytes(key, index);
    }
    return key;
}

Shader::SceneVertex PackVertex(const Domain::MeshPart& part, size_t i, bool skinned) {
    Shader::SceneVertex v{};
    const glm::vec3& p = part.positions[i];
    v.position[0] = p.x;
    v.position[1] = p.y;
    v.position[2] = p.z;

    const glm::vec3 n = i < part.normals.size() && part.normals.size() == part.positions.size()
                            ? SafeNormalize(part.normals[i], {0.0f, 0.0f, 1.0f})
                            : glm::vec3(0.0f, 0.0f, 1.0f);
    v.normal[0] = PackSnorm(n.x);
    v.normal[1] = PackSnorm(n.y);
    v.normal[2] = PackSnorm(n.z);

    glm::vec3 t(1.0f, 0.0f, 0.0f);
    float handedness = 1.0f;
    if (part.tangents.size() == part.positions.size()) {
        t = SafeNormalize(glm::vec3(part.tangents[i]), t);
        handedness = part.tangents[i].w < 0.0f ? -1.0f : 1.0f;
    }
    v.tangent[0] = PackSnorm(t.x);
    v.tangent[1] = PackSnorm(t.y);
    v.tangent[2] = PackSnorm(t.z);
    v.tangent[3] = PackSnorm(handedness);

    if (part.uv0.size() == part.positions.size()) {
        v.uv0[0] = part.uv0[i].x;
        v.uv0[1] = part.uv0[i].y;
    }
    if (part.uv1.size() == part.positions.size()) {
        v.uv1[0] = part.uv1[i].x;
        v.uv1[1] = part.uv1[i].y;
    }

    glm::vec4 c(1.0f);
    if (part.colors.size() == part.positions.size()) {
        c = part.colors[i];
    }
    for (int k = 0; k < 4; ++k) {
        v.color[k] = PackUnorm(c[k]);
    }

    if (skinned) {
        const size_t mapSize = part.jointMap.size();
        for (int k = 0; k < 4; ++k) {
            // A slot outside the joint map is read by the shader even at weight zero.
            v.joints[k] = part.joints[i][k] < mapSize ? part.joints[i][k] : uint16_t(0);
            v.weights[k] = std::isfinite(part.weights[i][k]) ? part.weights[i][k] : 0.0f;
        }
    }
    return v;
}

size_t AlignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

struct MaterialBuild {
    Domain::Material material;
    bool skinned = false;
    std::array<int32_t, Shader::kRoleCount> image{};
    uint32_t flags = 0;
};

struct PartSetBuild {
    size_t part = 0;
    size_t offset = 0;
    size_t size = 0;
};

}

SceneRenderer::SceneRenderer(Gpu::Device& device, const RendererOptions& options)
    : m_device(device), m_options(options) {}

SceneRenderer::~SceneRenderer() = default;

Result<std::unique_ptr<SceneRenderer>> SceneRenderer::Create(Gpu::Device& device, const RendererOptions& options) {
    std::unique_ptr<SceneRenderer> renderer(new SceneRenderer(device, options));

    auto pipelines = BuildScenePipelines(device, options);
    if (!pipelines.ok()) {
        return Result<std::unique_ptr<SceneRenderer>>::Fail(pipelines.error());
    }
    renderer->m_pipelines = std::make_unique<ScenePipelines>(std::move(pipelines.value()));

    constexpr uint32_t kSets = 64;
    const std::array frameSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kSets},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, kSets},
    };
    auto framePool = Gpu::DescriptorPool::Create(device, frameSizes, kSets);
    if (!framePool.ok()) {
        return Result<std::unique_ptr<SceneRenderer>>::Fail(framePool.error());
    }
    renderer->m_framePool.emplace(std::move(framePool.value()));

    const std::array backgroundSizes{VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kSets}};
    auto backgroundPool = Gpu::DescriptorPool::Create(device, backgroundSizes, kSets);
    if (!backgroundPool.ok()) {
        return Result<std::unique_ptr<SceneRenderer>>::Fail(backgroundPool.error());
    }
    renderer->m_backgroundPool.emplace(std::move(backgroundPool.value()));

    return Result<std::unique_ptr<SceneRenderer>>::Ok(std::move(renderer));
}

Result<GpuScene> SceneRenderer::Load(const Domain::Scene& scene, Gpu::UploadContext& upload, DiagSink* diags) {
    GpuScene out;
    out.m_device = &m_device;

    auto populated = Populate(out, scene, upload, diags);
    if (!populated.ok()) {
        // Copies recorded for buffers and images that `out` is about to retire must be submitted first.
        (void)upload.Flush();
        return Result<GpuScene>::Fail(populated.error());
    }

    auto flushed = upload.Flush();
    if (!flushed.ok()) {
        return Result<GpuScene>::Fail(flushed.error());
    }
    out.m_ready = flushed.value();
    return Result<GpuScene>::Ok(std::move(out));
}

Result<void> SceneRenderer::Populate(GpuScene& out, const Domain::Scene& scene, Gpu::UploadContext& upload,
                                     DiagSink* diags) {
    out.m_skeleton = scene.skeleton;
    out.m_animations = scene.animations;
    out.m_bounds = scene.bounds;
    out.m_transform = scene.transform;
    if (scene.skeleton) {
        out.m_palette = RestPalette(*scene.skeleton);
        out.m_worlds = RestWorldTransforms(*scene.skeleton);
    }

    // Skinning validation: report every diag, skip exactly the parts one of them names.
    std::vector<bool> rejected(scene.parts.size(), false);
    for (const Diag& diag : ValidateSkinning(scene)) {
        if (diags != nullptr) {
            diags->Report(diag);
        }
        if (diag.code == "render.skinning.order") {
            continue;
        }
        std::vector<size_t> named;
        for (size_t p = 0; p < scene.parts.size(); ++p) {
            if (diag.message.starts_with(PartLabel(scene.parts[p], p) + ":")) {
                named.push_back(p);
            }
        }
        if (named.size() == 1) {
            rejected[named.front()] = true;
            continue;
        }
        // Several parts share the label: validate each alone.
        for (size_t p : named) {
            Domain::Scene single;
            single.skeleton = scene.skeleton;
            single.parts.push_back(scene.parts[p]);
            for (const Diag& d : ValidateSkinning(single)) {
                if (d.code != "render.skinning.order") {
                    rejected[p] = true;
                }
            }
        }
    }

    Domain::Texture white;
    white.name = "default white";
    white.width = 1;
    white.height = 1;
    white.format = Domain::PixelFormat::Rgba8;
    white.pixels.assign(4, std::byte{0xFF});
    auto whiteImage = upload.Upload(white);
    if (!whiteImage.ok()) {
        return Result<void>::Fail(whiteImage.error());
    }
    out.m_images.push_back(std::move(whiteImage.value()));

    // The white default is image 0; scene textures are uploaded the first time a material needs them.
    std::vector<int32_t> textureImage(scene.textures.size(), kUntried);
    auto resolveTexture = [&](uint32_t index, const std::string& materialName, const char* role) -> Result<int32_t> {
        if (index >= scene.textures.size()) {
            Report(diags, Severity::Error, "render.material.index",
                   std::format("material '{}' {} texture index {} is outside the {} scene textures", materialName, role,
                               index, scene.textures.size()));
            return Result<int32_t>::Ok(kUseDefault);
        }
        int32_t& slot = textureImage[index];
        if (slot == kUntried) {
            auto image = upload.Upload(scene.textures[index]);
            if (image.ok()) {
                slot = static_cast<int32_t>(out.m_images.size());
                out.m_images.push_back(std::move(image.value()));
            } else if (image.error().code == "gpu.upload.size" || image.error().code == "gpu.upload.format") {
                Report(diags, Severity::Error, "render.texture.size",
                       std::format("texture {} '{}' was rejected ({}): {}", index, scene.textures[index].name,
                                   image.error().code, image.error().message));
                slot = kUseDefault;
            } else {
                return Result<int32_t>::Fail(image.error());
            }
        }
        return Result<int32_t>::Ok(slot);
    };

    std::vector<MaterialBuild> materials;
    std::unordered_map<std::string, uint32_t> materialLookup;
    std::vector<Shader::SceneVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<std::byte> partBytes(Shader::kBufferAlignment, std::byte{0});
    std::vector<PartSetBuild> partSets;
    const Domain::Material fallbackMaterial;

    for (size_t p = 0; p < scene.parts.size(); ++p) {
        const Domain::MeshPart& part = scene.parts[p];
        if (Domain::HasFlag(part.flags, Domain::PartFlags::Hidden) || rejected[p]) {
            continue;
        }
        const size_t vertexCount = part.positions.size();
        const size_t triangleCount = part.indices.size() / 3;
        if (vertexCount == 0 || triangleCount == 0) {
            continue;
        }
        const size_t indexCount = triangleCount * 3;
        if (vertices.size() + vertexCount > static_cast<size_t>(std::numeric_limits<int32_t>::max()) ||
            indices.size() + indexCount > std::numeric_limits<uint32_t>::max()) {
            return Result<void>::Fail({"gpu.upload.size", std::format("{} does not fit the scene buffers", PartLabel(part, p)), 0});
        }
        bool indicesValid = true;
        for (size_t i = 0; i < indexCount; ++i) {
            if (part.indices[i] >= vertexCount) {
                Report(diags, Severity::Error, "render.part.index",
                       std::format("{}: index {} at position {} is outside the {} vertices", PartLabel(part, p),
                                   part.indices[i], i, vertexCount));
                indicesValid = false;
                break;
            }
        }
        if (!indicesValid) {
            continue;
        }

        const bool skinned = scene.skeleton && !part.joints.empty() && !part.jointMap.empty();

        const bool materialValid = part.materialIndex < scene.materials.size();
        if (!materialValid) {
            Report(diags, Severity::Error, "render.material.index",
                   std::format("{}: material index {} is outside the {} scene materials", PartLabel(part, p),
                               part.materialIndex, scene.materials.size()));
        }
        const Domain::Material& material = materialValid ? scene.materials[part.materialIndex] : fallbackMaterial;

        const std::string key = MaterialKey(material, skinned);
        uint32_t materialSlot = 0;
        if (auto found = materialLookup.find(key); found != materialLookup.end()) {
            materialSlot = found->second;
        } else {
            MaterialBuild build;
            build.material = material;
            build.skinned = skinned;
            build.image.fill(kUseDefault);
            for (size_t r = 0; r < Shader::kRoleCount; ++r) {
                const auto entry = material.textures.find(kRoles[r]);
                if (entry == material.textures.end()) {
                    continue;
                }
                auto resolved = resolveTexture(entry->second, material.name, kRoleNames[r]);
                if (!resolved.ok()) {
                    return Result<void>::Fail(resolved.error());
                }
                build.image[r] = resolved.value();
                if (resolved.value() != kUseDefault) {
                    build.flags |= kRoleFlags[r];
                }
            }
            if (skinned) {
                build.flags |= Shader::Flag::kUseJoints;
            }
            if (material.normalEncoding == Domain::NormalEncoding::Xyz) {
                build.flags |= Shader::Flag::kNormalXYZ;
            }
            materialSlot = static_cast<uint32_t>(materials.size());
            materialLookup.emplace(key, materialSlot);
            materials.push_back(std::move(build));
        }
        const MaterialBuild& build = materials[materialSlot];

        GpuScene::Part gpuPart;
        gpuPart.firstIndex = static_cast<uint32_t>(indices.size());
        gpuPart.indexCount = static_cast<uint32_t>(indexCount);
        gpuPart.vertexOffset = static_cast<int32_t>(vertices.size());
        gpuPart.material = materialSlot;

        for (size_t i = 0; i < vertexCount; ++i) {
            vertices.push_back(PackVertex(part, i, skinned));
        }
        indices.insert(indices.end(), part.indices.begin(), part.indices.begin() + static_cast<ptrdiff_t>(indexCount));

        if (skinned) {
            PartSetBuild set;
            set.part = out.m_parts.size();
            set.offset = AlignUp(partBytes.size(), Shader::kBufferAlignment);
            set.size = part.jointMap.size() * sizeof(uint32_t);
            partBytes.resize(set.offset + set.size);
            for (size_t k = 0; k < part.jointMap.size(); ++k) {
                const uint32_t entry = part.jointMap[k];
                std::memcpy(partBytes.data() + set.offset + k * sizeof(uint32_t), &entry, sizeof(uint32_t));
            }
            partSets.push_back(set);
        }

        const bool sky = Domain::HasFlag(part.flags, Domain::PartFlags::Sky);
        if (sky) {
            gpuPart.kind = GpuScene::PipelineKind::Sky;
        } else if (material.blend == Domain::BlendMode::Opaque) {
            gpuPart.kind = GpuScene::PipelineKind::Opaque;
        } else if (material.blend == Domain::BlendMode::Additive) {
            gpuPart.kind = GpuScene::PipelineKind::Additive;
        } else {
            gpuPart.kind = GpuScene::PipelineKind::Alpha;
        }
        const uint32_t drawIndex = static_cast<uint32_t>(out.m_parts.size());
        if (sky) {
            out.m_sky.push_back(drawIndex);
        } else if (gpuPart.kind == GpuScene::PipelineKind::Opaque) {
            out.m_opaque.push_back(drawIndex);
        } else {
            out.m_blended.push_back(drawIndex);
        }
        out.m_parts.push_back(gpuPart);

        DrawInfo info;
        info.name = part.name;
        info.materialIndex = part.materialIndex;
        info.vertexCount = static_cast<uint32_t>(vertexCount);
        info.triangleCount = static_cast<uint32_t>(triangleCount);
        info.blend = material.blend;
        info.skinned = skinned;
        info.sky = sky;
        for (size_t r = 0; r < Shader::kRoleCount; ++r) {
            if (build.image[r] != kUseDefault) {
                ++info.texturesBound;
            }
        }
        info.jointCount = skinned ? static_cast<uint32_t>(part.jointMap.size()) : 0;
        out.m_draws.push_back(std::move(info));
    }

    auto sampler = Gpu::CreateSampler(m_device, {});
    if (!sampler.ok()) {
        return Result<void>::Fail(sampler.error());
    }
    out.m_sampler = std::move(sampler.value());

    if (out.m_parts.empty()) {
        return Result<void>::Ok();
    }

    auto vertexBuffer = upload.Upload(std::as_bytes(std::span<const Shader::SceneVertex>(vertices)),
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    if (!vertexBuffer.ok()) {
        return Result<void>::Fail(vertexBuffer.error());
    }
    out.m_vertices = std::move(vertexBuffer.value());

    auto indexBuffer = upload.Upload(std::as_bytes(std::span<const uint32_t>(indices)), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    if (!indexBuffer.ok()) {
        return Result<void>::Fail(indexBuffer.error());
    }
    out.m_indices = std::move(indexBuffer.value());

    std::vector<std::byte> materialBytes(materials.size() * Shader::kBufferAlignment, std::byte{0});
    for (size_t m = 0; m < materials.size(); ++m) {
        const Domain::Material& source = materials[m].material;
        Shader::MaterialUbo ubo{};
        ubo.baseColor = source.baseColor;
        ubo.tint = source.tint;
        ubo.uvOffset = source.uvOffset;
        ubo.metallic = source.metallic;
        ubo.flags = materials[m].flags;
        ubo.normalDecode = glm::vec4(2.0f, -1.0f, 1.0f, 1.0f);
        ubo.vertexColorScale = source.vertexColorScale;
        std::memcpy(materialBytes.data() + m * Shader::kBufferAlignment, &ubo, sizeof(ubo));
    }
    auto materialBuffer = upload.Upload(materialBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    if (!materialBuffer.ok()) {
        return Result<void>::Fail(materialBuffer.error());
    }
    out.m_materialData = std::move(materialBuffer.value());

    auto partBuffer = upload.Upload(partBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (!partBuffer.ok()) {
        return Result<void>::Fail(partBuffer.error());
    }
    out.m_partData = std::move(partBuffer.value());

    // Descriptor sets: one per distinct material, one per skinned part, one shared by the rest.
    const uint32_t materialCount = static_cast<uint32_t>(materials.size());
    const uint32_t partSetCount = static_cast<uint32_t>(partSets.size()) + 1;
    const std::array poolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, materialCount},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             materialCount * static_cast<uint32_t>(Shader::kRoleCount)},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, partSetCount},
    };
    auto pool = Gpu::DescriptorPool::Create(m_device, poolSizes, materialCount + partSetCount);
    if (!pool.ok()) {
        return Result<void>::Fail(pool.error());
    }
    out.m_pool.emplace(std::move(pool.value()));

    const VkDescriptorSetLayout materialLayout = m_pipelines->opaque.setLayouts[1].Get();
    const VkDescriptorSetLayout partLayout = m_pipelines->opaque.setLayouts[2].Get();
    const auto& fn = m_device.Fn();

    for (uint32_t m = 0; m < materialCount; ++m) {
        auto set = out.m_pool->Allocate(materialLayout);
        if (!set.ok()) {
            return Result<void>::Fail(set.error());
        }
        out.m_materials.push_back({set.value()});

        VkDescriptorBufferInfo bufferInfo{out.m_materialData.handle.Get(), m * Shader::kBufferAlignment,
                                          sizeof(Shader::MaterialUbo)};
        std::array<VkDescriptorImageInfo, Shader::kRoleCount> imageInfos{};
        for (size_t r = 0; r < Shader::kRoleCount; ++r) {
            const int32_t image = materials[m].image[r] == kUseDefault ? 0 : materials[m].image[r];
            imageInfos[r].sampler = out.m_sampler.Get();
            imageInfos[r].imageView = out.m_images[static_cast<size_t>(image)].view.Get();
            imageInfos[r].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = set.value();
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &bufferInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = set.value();
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = static_cast<uint32_t>(Shader::kRoleCount);
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = imageInfos.data();
        fn.vkUpdateDescriptorSets(m_device.Handle(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    auto writePartSet = [&](size_t offset, size_t size) -> Result<VkDescriptorSet> {
        auto set = out.m_pool->Allocate(partLayout);
        if (!set.ok()) {
            return set;
        }
        VkDescriptorBufferInfo bufferInfo{out.m_partData.handle.Get(), offset, size};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set.value();
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &bufferInfo;
        fn.vkUpdateDescriptorSets(m_device.Handle(), 1, &write, 0, nullptr);
        return set;
    };

    auto defaultSet = writePartSet(0, 16);
    if (!defaultSet.ok()) {
        return Result<void>::Fail(defaultSet.error());
    }
    for (GpuScene::Part& part : out.m_parts) {
        part.partSet = defaultSet.value();
    }
    for (const PartSetBuild& build : partSets) {
        auto set = writePartSet(build.offset, build.size);
        if (!set.ok()) {
            return Result<void>::Fail(set.error());
        }
        out.m_parts[build.part].partSet = set.value();
    }
    return Result<void>::Ok();
}

Result<VkDescriptorSet> SceneRenderer::FrameSet(VkBuffer transient, VkDeviceSize paletteRange) {
    const auto key = std::make_pair(transient, paletteRange);
    if (auto found = m_frameSets.find(key); found != m_frameSets.end()) {
        return Result<VkDescriptorSet>::Ok(found->second);
    }
    auto set = m_framePool->Allocate(m_pipelines->opaque.setLayouts[0].Get());
    if (!set.ok()) {
        return set;
    }
    VkDescriptorBufferInfo uboInfo{transient, 0, sizeof(Shader::FrameUbo)};
    VkDescriptorBufferInfo paletteInfo{transient, 0, paletteRange};
    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set.value();
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[0].pBufferInfo = &uboInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set.value();
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    writes[1].pBufferInfo = &paletteInfo;
    m_device.Fn().vkUpdateDescriptorSets(m_device.Handle(), static_cast<uint32_t>(writes.size()), writes.data(), 0,
                                         nullptr);
    m_frameSets.emplace(key, set.value());
    return set;
}

Result<VkDescriptorSet> SceneRenderer::BackgroundSet(VkBuffer transient) {
    if (auto found = m_backgroundSets.find(transient); found != m_backgroundSets.end()) {
        return Result<VkDescriptorSet>::Ok(found->second);
    }
    auto set = m_backgroundPool->Allocate(m_pipelines->background.setLayouts[0].Get());
    if (!set.ok()) {
        return set;
    }
    VkDescriptorBufferInfo info{transient, 0, sizeof(Shader::BackgroundUbo)};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set.value();
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    write.pBufferInfo = &info;
    m_device.Fn().vkUpdateDescriptorSets(m_device.Handle(), 1, &write, 0, nullptr);
    m_backgroundSets.emplace(transient, set.value());
    return set;
}

Result<void> SceneRenderer::Draw(Gpu::Frame& frame, Gpu::RenderTarget& target, const GpuScene& scene, const View& view,
                                 const Overlays& overlays, DiagSink* diags) {
    const Gpu::RenderTargetDesc& desc = target.Desc();
    if (desc.colorFormat != m_options.colorFormat || desc.depthFormat != m_options.depthFormat ||
        desc.samples != m_options.samples) {
        return Result<void>::Fail({"render.target.format",
                                   "the render target's formats or sample count differ from the renderer's options", 0});
    }
    if (scene.m_device != &m_device) {
        return Result<void>::Fail({"render.scene.device", "the scene was loaded on another device", 0});
    }
    if (scene.m_paletteRejected) {
        Report(diags, Severity::Warning, "render.palette.size",
               "SetPalette was given a palette whose size differs from the skeleton's joint count and was ignored");
    }

    // Every fallible step precedes the first recorded command, so a failure leaves the command
    // buffer outside a render pass.
    const bool hasSky = !scene.m_sky.empty();
    const glm::mat4 skyView = glm::mat4(glm::mat3(view.view));
    const int32_t shading = view.mode == ShadingMode::Textured ? Shader::kShadingTextured : Shader::kShadingSolid;

    auto mainUbo = frame.Allocate(sizeof(Shader::FrameUbo), Shader::kBufferAlignment);
    if (!mainUbo.ok()) {
        return Result<void>::Fail(mainUbo.error());
    }
    Shader::FrameUbo mainData{};
    mainData.view = view.view;
    mainData.proj = view.proj.matrix;
    mainData.cameraPos = glm::vec3(glm::inverse(view.view)[3]);
    mainData.shadingMode = shading;
    std::memcpy(mainUbo.value().mapped, &mainData, sizeof(mainData));

    Result<Gpu::Transient> skyUbo = mainUbo;
    if (hasSky) {
        skyUbo = frame.Allocate(sizeof(Shader::FrameUbo), Shader::kBufferAlignment);
        if (!skyUbo.ok()) {
            return Result<void>::Fail(skyUbo.error());
        }
        Shader::FrameUbo skyData = mainData;
        skyData.view = skyView;
        skyData.cameraPos = glm::vec3(glm::inverse(skyView)[3]);
        std::memcpy(skyUbo.value().mapped, &skyData, sizeof(skyData));
    }

    const glm::mat4 identity(1.0f);
    const std::span<const glm::mat4> palette = scene.m_palette.empty()
                                                   ? std::span<const glm::mat4>(&identity, 1)
                                                   : std::span<const glm::mat4>(scene.m_palette);
    // A dynamic storage descriptor needs a fixed range, so the allocation is padded to a power of two
    // and one frame set exists per range.
    const VkDeviceSize paletteRange = std::bit_ceil(std::max<size_t>(palette.size_bytes(), 4096));
    auto paletteBuffer = frame.Allocate(paletteRange, Shader::kBufferAlignment);
    if (!paletteBuffer.ok()) {
        return Result<void>::Fail(paletteBuffer.error());
    }
    std::memcpy(paletteBuffer.value().mapped, palette.data(), palette.size_bytes());

    std::optional<Gpu::Transient> backgroundUbo;
    if (view.background) {
        auto allocated = frame.Allocate(sizeof(Shader::BackgroundUbo), Shader::kBufferAlignment);
        if (!allocated.ok()) {
            return Result<void>::Fail(allocated.error());
        }
        Shader::BackgroundUbo data{};
        data.top = glm::vec4(view.background->top, 1.0f);
        data.bottom = glm::vec4(view.background->bottom, 1.0f);
        std::memcpy(allocated.value().mapped, &data, sizeof(data));
        backgroundUbo = allocated.value();
    }

    const VkBuffer transientBuffer = mainUbo.value().buffer;
    auto frameSet = FrameSet(transientBuffer, paletteRange);
    if (!frameSet.ok()) {
        return Result<void>::Fail(frameSet.error());
    }
    VkDescriptorSet backgroundSet = VK_NULL_HANDLE;
    if (backgroundUbo) {
        auto set = BackgroundSet(backgroundUbo->buffer);
        if (!set.ok()) {
            return Result<void>::Fail(set.error());
        }
        backgroundSet = set.value();
    }

    frame.AddWait(scene.Ready());

    const auto& fn = m_device.Fn();
    const VkCommandBuffer cmd = frame.Cmd();
    target.Begin(cmd, view.clear);

    if (backgroundUbo) {
        const uint32_t offset = static_cast<uint32_t>(backgroundUbo->offset);
        fn.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines->background.pipeline.Get());
        fn.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines->background.layout.Get(), 0, 1,
                                   &backgroundSet, 1, &offset);
        fn.vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    if (!scene.m_parts.empty()) {
        const VkPipelineLayout layout = m_pipelines->opaque.layout.Get();
        const Shader::ScenePush push{scene.m_transform};
        fn.vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        const VkDeviceSize zero = 0;
        const VkBuffer vertexBuffer = scene.m_vertices.handle.Get();
        fn.vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &zero);
        fn.vkCmdBindIndexBuffer(cmd, scene.m_indices.handle.Get(), 0, VK_INDEX_TYPE_UINT32);

        VkPipeline boundPipeline = VK_NULL_HANDLE;
        VkDescriptorSet boundMaterial = VK_NULL_HANDLE;
        VkDescriptorSet boundPart = VK_NULL_HANDLE;
        auto pipelineFor = [&](GpuScene::PipelineKind kind) {
            switch (kind) {
            case GpuScene::PipelineKind::Sky:
                return m_pipelines->sky.pipeline.Get();
            case GpuScene::PipelineKind::Opaque:
                return m_pipelines->opaque.pipeline.Get();
            case GpuScene::PipelineKind::Alpha:
                return m_pipelines->alpha.pipeline.Get();
            case GpuScene::PipelineKind::Additive:
                break;
            }
            return m_pipelines->additive.pipeline.Get();
        };
        auto bindFrame = [&](const Gpu::Transient& ubo) {
            const std::array<uint32_t, 2> offsets{static_cast<uint32_t>(ubo.offset),
                                                  static_cast<uint32_t>(paletteBuffer.value().offset)};
            fn.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &frameSet.value(),
                                       static_cast<uint32_t>(offsets.size()), offsets.data());
        };
        auto drawList = [&](const std::vector<uint32_t>& list) {
            for (uint32_t index : list) {
                const GpuScene::Part& part = scene.m_parts[index];
                const VkPipeline pipeline = pipelineFor(part.kind);
                if (pipeline != boundPipeline) {
                    fn.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    boundPipeline = pipeline;
                }
                const VkDescriptorSet material = scene.m_materials[part.material].set;
                if (material != boundMaterial) {
                    fn.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &material, 0,
                                               nullptr);
                    boundMaterial = material;
                }
                if (part.partSet != boundPart) {
                    fn.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 2, 1, &part.partSet, 0,
                                               nullptr);
                    boundPart = part.partSet;
                }
                fn.vkCmdDrawIndexed(cmd, part.indexCount, 1, part.firstIndex, part.vertexOffset, 0);
            }
        };

        if (hasSky) {
            bindFrame(skyUbo.value());
            drawList(scene.m_sky);
        }
        bindFrame(mainUbo.value());
        drawList(scene.m_opaque);
        drawList(scene.m_blended);
    }

    DrawOverlays(cmd, scene, view, overlays);
    target.End(cmd);
    return Result<void>::Ok();
}

void SceneRenderer::DrawOverlays(VkCommandBuffer, const GpuScene&, const View&, const Overlays&) {}

}
