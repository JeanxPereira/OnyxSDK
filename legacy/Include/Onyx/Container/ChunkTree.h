#pragma once
#include <Onyx/Container/ChunkReader.h>
#include <Onyx/Container/ChunkSchema.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Onyx::Container {

// A node in a parsed chunk hierarchy. Holds positions, not bytes (zero-copy):
// the caller keeps the source buffer; nodes reference into it via offsets.
struct ChunkNode {
    std::string tag;
    uint64_t    offset      = 0;   // header start
    uint64_t    dataOffset  = 0;   // payload start
    uint64_t    size        = 0;   // payload size
    bool        isContainer = false;
    std::vector<ChunkNode> children;
};

// Parse [off, end) into top-level ChunkNodes; descend into schema containers,
// bounded to each container's payload. Unknown/oversized chunks stay leaves.
std::vector<ChunkNode> BuildChunkTree(const ChunkReader& reader, const ChunkSchema& schema,
                                      std::span<const uint8_t> buf,
                                      uint64_t off = 0, uint64_t end = 0);

} // namespace Onyx::Container
