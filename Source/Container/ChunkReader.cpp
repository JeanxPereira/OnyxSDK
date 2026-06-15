#include <Onyx/Container/ChunkReader.h>

namespace Onyx::Container {

namespace {
uint32_t ReadU32(const uint8_t* p, ChunkFormat::Endian e) {
    if (e == ChunkFormat::Endian::Big)
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    return (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0];
}
} // namespace

ChunkHeader ChunkReader::ReadHeader(std::span<const uint8_t> buf, uint64_t off) const {
    ChunkHeader h;
    h.offset = off;
    const uint8_t headerSize = static_cast<uint8_t>(m_fmt.tagBytes + 4);
    const uint8_t* base = buf.data() + off;
    uint32_t raw;
    if (m_fmt.sizeBeforeTag) {
        raw = ReadU32(base, m_fmt.sizeEndian);
        h.tag.assign(reinterpret_cast<const char*>(base + 4), m_fmt.tagBytes);
    } else {
        h.tag.assign(reinterpret_cast<const char*>(base), m_fmt.tagBytes);
        raw = ReadU32(base + m_fmt.tagBytes, m_fmt.sizeEndian);
    }
    h.dataOffset = off + headerSize;
    h.size = m_fmt.sizeIncludesHeader
                 ? (raw >= headerSize ? raw - headerSize : 0)
                 : raw;
    return h;
}

std::vector<ChunkHeader> ChunkReader::ReadChunks(std::span<const uint8_t> buf,
                                                 uint64_t off, uint64_t end) const {
    std::vector<ChunkHeader> out;
    const uint8_t headerSize = static_cast<uint8_t>(m_fmt.tagBytes + 4);
    const uint64_t limit = (end == 0 || end > buf.size()) ? buf.size() : end;
    uint64_t cur = off;
    while (cur + headerSize <= limit) {
        ChunkHeader h = ReadHeader(buf, cur);
        out.push_back(h);
        uint64_t next = cur + headerSize + h.size;
        if (m_fmt.alignment > 1)
            next = (next + m_fmt.alignment - 1) / m_fmt.alignment * m_fmt.alignment;
        if (next <= cur) break;     // tolerant: never loop forever on a 0-advance chunk
        cur = next;
    }
    return out;
}

} // namespace Onyx::Container
