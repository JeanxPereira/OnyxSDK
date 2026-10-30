#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Onyx::Domain {

template <class T>
struct Key {
    float time = 0.0f;
    T value{};
};

struct AnimChannel {
    uint32_t joint = 0;
    std::vector<Key<glm::quat>> rotation;
    std::vector<Key<glm::vec3>> translation;
    std::vector<Key<glm::vec3>> scale;
};

struct AnimClip {
    std::string name;
    float duration = 0.0f;
    std::vector<AnimChannel> channels;
    bool additive = false;
};

struct AnimationSet {
    std::vector<AnimClip> clips;
};

}
