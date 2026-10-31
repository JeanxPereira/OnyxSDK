#pragma once

#include <Onyx/Core/Domain/Skeleton.h>
#include <Onyx/Gpu/FrameRing.h>
#include <Onyx/Render/View.h>

#include <glm/glm.hpp>

#include <optional>
#include <span>
#include <vector>

#include "ShaderTypes.h"

namespace Onyx::Render {

// Transient allocations and descriptor sets of one frame's overlays, resolved before the first
// command is recorded.
struct OverlayPlan {
    std::optional<Gpu::Transient> grid;
    VkDescriptorSet gridSet = VK_NULL_HANDLE;
    std::optional<Gpu::Transient> skeletonUbo;
    VkDescriptorSet skeletonSet = VK_NULL_HANDLE;
    Gpu::Transient vertices;
    uint32_t vertexCount = 0;
};

Shader::GridUbo MakeGridUbo(const View& view, const Overlays& overlays);

// Bone segments from each joint to its parent, a cross at each root, a small marker and the
// orientation axes at each joint, in world space after `model`. Empty when `worlds` does not hold
// one transform per joint.
std::vector<Shader::OverlayVertex> BuildSkeletonLines(const Domain::Skeleton& skeleton,
                                                      std::span<const glm::mat4> worlds, const glm::mat4& model,
                                                      const glm::vec4& boneColor);

}
