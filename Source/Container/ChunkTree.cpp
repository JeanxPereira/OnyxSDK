#include <Onyx/Container/ChunkTree.h>

#include <utility>

namespace Onyx::Container {

std::vector<ChunkNode> BuildChunkTree(const ChunkReader& reader, const ChunkSchema& schema,
                                      std::span<const uint8_t> buf, uint64_t off, uint64_t end) {
    std::vector<ChunkNode> nodes;
    for (const ChunkHeader& h : reader.ReadChunks(buf, off, end)) {
        ChunkNode n;
        n.tag = h.tag;
        n.offset = h.offset;
        n.dataOffset = h.dataOffset;
        n.size = h.size;

        const bool inBounds = h.dataOffset + h.size <= buf.size();
        n.isContainer = inBounds && schema.IsContainer(h.tag);
        if (n.isContainer)
            n.children = BuildChunkTree(reader, schema, buf, h.dataOffset, h.dataOffset + h.size);

        nodes.push_back(std::move(n));
    }
    return nodes;
}

} // namespace Onyx::Container
