#pragma once

#include <cstdint>
#include <vector>

namespace Onyx::Domain {

struct Audio {
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    std::vector<float> samples;
};

}
