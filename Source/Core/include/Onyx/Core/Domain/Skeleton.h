#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Onyx::Domain {

struct Joint {
    std::string name;
    int32_t parent = -1;
    glm::mat4 localBind{1.0f};
    glm::mat4 inverseBind{1.0f};
};

struct Skeleton {
    std::vector<Joint> joints;
};

}
