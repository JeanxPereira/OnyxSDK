#include <doctest/doctest.h>

#include <cstdint>
#include <span>
#include <vector>

#include <Onyx/Container/ChunkReader.h>
#include <Onyx/Container/ChunkSchema.h>
#include <Onyx/Container/ChunkTree.h>

using Onyx::Container::ChunkFormat;
using Onyx::Container::ChunkReader;
using Onyx::Container::ChunkSchema;
using Onyx::Container::BuildChunkTree;

// "ROOT" (container, size 24 incl header) holding two leaf chunks "LEAA","LEAB" (size 8 each).
static const std::vector<uint8_t> kBuf = {
    'R','O','O','T', 0x00,0x00,0x00,0x18,
        'L','E','A','A', 0x00,0x00,0x00,0x08,
        'L','E','A','B', 0x00,0x00,0x00,0x08,
};

TEST_CASE("BuildChunkTree descends containers and bounds to payload") {
    ChunkReader reader{ChunkFormat{}};
    ChunkSchema schema;
    schema.SetContainer("ROOT", {"LEAA", "LEAB"});

    auto nodes = BuildChunkTree(reader, schema, std::span<const uint8_t>(kBuf));

    REQUIRE(nodes.size() == 1);
    CHECK(nodes[0].tag == "ROOT");
    CHECK(nodes[0].isContainer);
    REQUIRE(nodes[0].children.size() == 2);
    CHECK(nodes[0].children[0].tag == "LEAA");
    CHECK_FALSE(nodes[0].children[0].isContainer);
    CHECK(nodes[0].children[1].tag == "LEAB");
    CHECK(nodes[0].children[1].dataOffset == 24);  // 8 (ROOT hdr) + 8 (LEAA) + 8 (LEAB hdr)
}

TEST_CASE("BuildChunkTree treats unknown tags as leaves (no throw)") {
    ChunkReader reader{ChunkFormat{}};
    ChunkSchema schema;                            // empty: nothing is a container
    auto nodes = BuildChunkTree(reader, schema, std::span<const uint8_t>(kBuf));

    REQUIRE(nodes.size() == 1);
    CHECK(nodes[0].tag == "ROOT");
    CHECK_FALSE(nodes[0].isContainer);             // not in schema → opaque leaf
    CHECK(nodes[0].children.empty());
}
