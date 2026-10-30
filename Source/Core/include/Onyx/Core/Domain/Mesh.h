#pragma once

#include <Onyx/Core/Domain/BoundingBox.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Onyx::Domain {

enum class PartFlags : uint8_t {
    None = 0,
    Sky = 1,
    Hidden = 2,
};

constexpr PartFlags operator|(PartFlags a, PartFlags b) {
    return static_cast<PartFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr PartFlags operator&(PartFlags a, PartFlags b) {
    return static_cast<PartFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr bool HasFlag(PartFlags value, PartFlags flag) {
    return (value & flag) == flag;
}

struct MeshPart {
    std::string name;
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec4> tangents;
    std::vector<glm::vec2> uv0, uv1;
    std::vector<glm::vec4> colors;
    std::vector<glm::u16vec4> joints;
    std::vector<glm::vec4> weights;
    std::vector<uint32_t> indices;
    std::vector<uint16_t> jointMap;
    uint32_t materialIndex = 0;
    PartFlags flags = PartFlags::None;
    uint64_t userTag = 0;
    BoundingBox bounds;
};

}
