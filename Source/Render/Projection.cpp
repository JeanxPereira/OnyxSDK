#include "Onyx/Render/Projection.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Onyx::Render {

Projection Projection::Perspective(float fovYRadians, float aspect, float nearPlane, float farPlane) {
    Projection p;
    p.matrix = glm::perspective(fovYRadians, aspect, nearPlane, farPlane);
    p.matrix[1][1] = -p.matrix[1][1];
    return p;
}

Projection Projection::Orthographic(float left, float right, float bottom, float top, float nearPlane,
                                    float farPlane) {
    Projection p;
    p.matrix = glm::ortho(left, right, bottom, top, nearPlane, farPlane);
    p.matrix[1][1] = -p.matrix[1][1];
    return p;
}

}
