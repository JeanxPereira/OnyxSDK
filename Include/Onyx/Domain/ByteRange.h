#pragma once
#include <cstdint>
namespace Onyx::Domain {
/// Where an entry's payload physically lives. fileIndex indexes the owning
/// Document's file table: 0 = the container file itself, 1+ = files opened
/// through the document's mount (Task 7). Empty() ranges carry no payload.
struct ByteRange {
    uint32_t fileIndex = 0;
    uint64_t offset    = 0;
    uint64_t size      = 0;
    bool Empty() const { return size == 0; }
};
}
