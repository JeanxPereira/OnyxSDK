#include <Onyx/Core/Domain/Scene.h>

namespace Onyx::Domain {

BoundingBox Scene::ComputeBounds() const {
    std::vector<glm::vec3> points;
    for (const MeshPart& part : parts) {
        for (const glm::vec3& p : part.positions) {
            points.push_back(glm::vec3(transform * glm::vec4(p, 1.0f)));
        }
    }
    return BoundingBox::Of(points);
}

}
