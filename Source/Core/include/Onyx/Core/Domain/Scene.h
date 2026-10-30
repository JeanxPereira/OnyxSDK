#pragma once

#include <Onyx/Core/Domain/Animation.h>
#include <Onyx/Core/Domain/BoundingBox.h>
#include <Onyx/Core/Domain/Material.h>
#include <Onyx/Core/Domain/Mesh.h>
#include <Onyx/Core/Domain/Skeleton.h>
#include <Onyx/Core/Domain/Texture.h>

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace Onyx::Domain {

struct Scene {
    glm::mat4 transform{1.0f};
    std::shared_ptr<const Skeleton> skeleton;
    std::shared_ptr<const AnimationSet> animations;
    std::vector<MeshPart> parts;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    BoundingBox bounds;

    BoundingBox ComputeBounds() const;
};

}
