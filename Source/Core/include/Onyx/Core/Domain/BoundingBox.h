#pragma once

#include <glm/glm.hpp>

#include <span>

namespace Onyx::Domain {

struct BoundingBox {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    glm::vec3 Center() const { return (min + max) * 0.5f; }
    float Radius() const { return glm::length(max - min) * 0.5f; }

    static BoundingBox Of(std::span<const glm::vec3> points) {
        BoundingBox box;
        if (points.empty()) {
            return box;
        }
        box.min = box.max = points.front();
        for (const glm::vec3& p : points) {
            box.min = glm::min(box.min, p);
            box.max = glm::max(box.max, p);
        }
        return box;
    }
};

}
