#pragma once

#include <glm/glm.hpp>

namespace Onyx::Render {

// Clip-space convention of the renderer: the Vulkan Y flip is baked into the matrix, so no caller
// flips again.
struct Projection {
    glm::mat4 matrix{1.0f};

    static Projection Perspective(float fovYRadians, float aspect, float nearPlane, float farPlane);
    static Projection Orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane);
};

}
