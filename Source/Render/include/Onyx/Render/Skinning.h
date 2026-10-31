#pragma once

#include <Onyx/Core/Domain/Scene.h>
#include <Onyx/Core/Domain/Skeleton.h>
#include <Onyx/Core/Services/Diagnostics.h>

#include <glm/glm.hpp>

#include <span>
#include <vector>

namespace Onyx::Render {

// Joints are ordered so that a parent precedes its children; a parent index outside [0, joint)
// makes the joint a root.

// world(j) = world(parent) * localBind
std::vector<glm::mat4> RestWorldTransforms(const Domain::Skeleton& skeleton);

// palette(j) = world(j) * inverseBind
std::vector<glm::mat4> RestPalette(const Domain::Skeleton& skeleton);

// The translation of each world transform, in joint order.
std::vector<glm::vec3> JointWorldPositions(const Domain::Skeleton& skeleton,
                                           std::span<const glm::mat4> worldTransforms);

// The renderer parses the "<part>:" prefix of each message (part 'name' or part N) to find the part.
// One Error per offending part, with the first violation found: render.skinning.streams,
// render.skinning.jointMap or render.skinning.jointIndex.
std::vector<Services::Diag> ValidateSkinning(const Domain::Scene& scene);

}
