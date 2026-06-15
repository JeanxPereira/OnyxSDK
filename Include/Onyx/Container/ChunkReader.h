#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Onyx::Container {

struct ChunkFormat {
    uint8_t tagBytes = 4;                 // 4 (v5+/v8) or 2 (old SCUMM/earwax)
    enum class Endian { Big, Little };
    Endian   sizeEndian         = Endian::Big;
    bool     sizeIncludesHeader = true;   // size field counts the tag+size header
    bool     sizeBeforeTag      = false;  // earwax: <size(LE u32)><tag(2)>
    uint32_t alignment          = 1;      // SCUMM = 2
};

struct ChunkHeader {
    std::string tag;             // ASCII, `tagBytes` chars
    uint64_t    offset     = 0;  // header start in the buffer
    uint64_t    dataOffset = 0;  // payload start
    uint64_t    size       = 0;  // payload size (header excluded)
};

class ChunkReader {
public:
    explicit ChunkReader(ChunkFormat fmt) : m_fmt(fmt) {}

    ChunkHeader ReadHeader(std::span<const uint8_t> buf, uint64_t off) const;
    // Parse sequential chunks in [off, end). end == 0 means buf.size().
    std::vector<ChunkHeader> ReadChunks(std::span<const uint8_t> buf,
                                        uint64_t off = 0, uint64_t end = 0) const;

private:
    ChunkFormat m_fmt;
};

} // namespace Onyx::Container
