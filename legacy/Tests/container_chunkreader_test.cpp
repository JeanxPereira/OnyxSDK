#include <doctest/doctest.h>

#include <cstdint>
#include <span>
#include <vector>

#include <Onyx/Container/ChunkReader.h>

using Onyx::Container::ChunkFormat;
using Onyx::Container::ChunkReader;

// Two IFF chunks, 4-byte tag + big-endian size that INCLUDES the 8-byte header.
//  "ABCD" size 12 (8 header + 4 payload DE AD BE EF), then "WXYZ" size 8 (no payload).
static const std::vector<uint8_t> kBuf = {
    'A','B','C','D', 0x00,0x00,0x00,0x0C, 0xDE,0xAD,0xBE,0xEF,
    'W','X','Y','Z', 0x00,0x00,0x00,0x08,
};

TEST_CASE("ChunkReader parses BE 4CC chunks (inclheader)") {
    ChunkFormat fmt;                 // defaults: tagBytes=4, BigEndian, inclheader, align 1
    ChunkReader reader(fmt);
    auto chunks = reader.ReadChunks(std::span<const uint8_t>(kBuf));

    REQUIRE(chunks.size() == 2);
    CHECK(chunks[0].tag == "ABCD");
    CHECK(chunks[0].offset == 0);
    CHECK(chunks[0].dataOffset == 8);
    CHECK(chunks[0].size == 4);
    CHECK(chunks[1].tag == "WXYZ");
    CHECK(chunks[1].offset == 12);
    CHECK(chunks[1].dataOffset == 20);
    CHECK(chunks[1].size == 0);
}

TEST_CASE("ChunkReader stops cleanly at the end / respects end bound") {
    ChunkFormat fmt;
    ChunkReader reader(fmt);
    // Bound parsing to the first chunk only (end = 12).
    auto chunks = reader.ReadChunks(std::span<const uint8_t>(kBuf), 0, 12);
    REQUIRE(chunks.size() == 1);
    CHECK(chunks[0].tag == "ABCD");
}
