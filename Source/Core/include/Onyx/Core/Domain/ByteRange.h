#pragma once

#include <cstdint>

namespace Onyx::Domain {

struct ByteRange {
    uint32_t fileIndex = 0;
    uint64_t offset = 0;
    uint64_t size = 0;

    bool Empty() const { return size == 0; }
};

}
