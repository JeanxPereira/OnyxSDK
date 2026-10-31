#pragma once

#include <Onyx/Render/Projection.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>

namespace Onyx::Render {

enum class ShadingMode : uint8_t { Solid, Textured };

struct Gradient {
    glm::vec3 top;
    glm::vec3 bottom;
};

struct ClipTime {
    uint32_t clip = 0;
    float time = 0.0f;
};

struct View {
    glm::mat4 view{1.0f};
    Projection proj;
    ShadingMode mode = ShadingMode::Solid;
    glm::vec4 clear{0.10f, 0.11f, 0.13f, 1.0f};
    std::optional<Gradient> background;
    std::optional<ClipTime> pose;
};

struct Overlays {
    bool grid = false;
    bool skeleton = false;
    glm::vec4 gridColor{1, 1, 1, 1};
    glm::vec4 boneColor{0.0f, 1.0f, 0.4f, 1.0f};
    float gridScale = 1.0f;
};

}
