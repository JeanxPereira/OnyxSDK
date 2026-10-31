#pragma once

#include <Onyx/Core/Domain/Animation.h>
#include <Onyx/Core/Domain/BoundingBox.h>
#include <Onyx/Core/Domain/Material.h>
#include <Onyx/Core/Domain/Skeleton.h>
#include <Onyx/Gpu/Descriptors.h>
#include <Onyx/Gpu/Device.h>
#include <Onyx/Gpu/Handles.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Onyx::Render {

struct DrawInfo {
    std::string name;
    uint32_t materialIndex = 0;
    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;
    Domain::BlendMode blend = Domain::BlendMode::Opaque;
    bool skinned = false;
    bool sky = false;
    uint32_t texturesBound = 0;
    uint32_t jointCount = 0;
};

// A scene resident on the GPU: one vertex and one index buffer, one descriptor set per distinct
// material and one per skinned part. Move-only; destruction retires every resource through the
// device. Must be destroyed before the Device it was loaded on. Not thread-safe.
class GpuScene {
public:
    GpuScene(GpuScene&&) noexcept = default;
    GpuScene& operator=(GpuScene&&) noexcept = default;
    GpuScene(const GpuScene&) = delete;
    GpuScene& operator=(const GpuScene&) = delete;
    ~GpuScene() = default;

    // One entry per uploaded part, in scene order. Hidden, skipped and empty parts are absent.
    // DrawInfo::materialIndex is the index into the source scene's materials.
    std::span<const DrawInfo> Draws() const { return m_draws; }
    Domain::BoundingBox Bounds() const { return m_bounds; }
    // The upload flush value; Draw makes its frame wait on it.
    Gpu::FenceValue Ready() const { return m_ready; }
    std::shared_ptr<const Domain::Skeleton> Skeleton() const { return m_skeleton; }
    std::shared_ptr<const Domain::AnimationSet> Animations() const { return m_animations; }
    // Distinct materials after deduplication.
    uint32_t MaterialCount() const { return static_cast<uint32_t>(m_materials.size()); }

    // CPU copy of the joint palette, one matrix per skeleton joint. A span of any other size is
    // ignored and reported by the next SceneRenderer::Draw as render.palette.size.
    void SetPalette(std::span<const glm::mat4> palette);
    // The world transform of each joint, kept for the skeleton overlay.
    void SetWorldTransforms(std::span<const glm::mat4> worlds);
    // Defaults to the rest palette of the skeleton; empty without one.
    std::span<const glm::mat4> Palette() const { return m_palette; }
    std::span<const glm::mat4> WorldTransforms() const { return m_worlds; }

private:
    friend class SceneRenderer;
    GpuScene() = default;

    enum class PipelineKind : uint8_t { Sky, Opaque, Alpha, Additive };

    struct Part {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int32_t vertexOffset = 0;
        uint32_t material = 0;
        VkDescriptorSet partSet = VK_NULL_HANDLE;
        PipelineKind kind = PipelineKind::Opaque;
    };

    struct Material {
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    Gpu::Device* m_device = nullptr;
    Gpu::Buffer m_vertices;
    Gpu::Buffer m_indices;
    Gpu::Buffer m_materialData;
    Gpu::Buffer m_partData;
    std::vector<Gpu::Image> m_images;
    Gpu::Unique<VkSampler> m_sampler;
    std::optional<Gpu::DescriptorPool> m_pool;
    std::vector<Part> m_parts;
    std::vector<Material> m_materials;
    std::vector<DrawInfo> m_draws;
    std::vector<uint32_t> m_sky;
    std::vector<uint32_t> m_opaque;
    std::vector<uint32_t> m_blended;
    glm::mat4 m_transform{1.0f};
    Domain::BoundingBox m_bounds;
    Gpu::FenceValue m_ready = 0;
    std::shared_ptr<const Domain::Skeleton> m_skeleton;
    std::shared_ptr<const Domain::AnimationSet> m_animations;
    std::vector<glm::mat4> m_palette;
    std::vector<glm::mat4> m_worlds;
    bool m_paletteRejected = false;
};

}
