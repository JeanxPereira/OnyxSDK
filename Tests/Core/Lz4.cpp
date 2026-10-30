#include <doctest/doctest.h>

#include <Onyx/Core/Vfs/Lz4.h>
#include <Onyx/TestKit/Lz4Fixtures.h>

#include <cstddef>
#include <vector>

using namespace Onyx::Vfs;

namespace {

std::vector<std::byte> Sample() {
    std::vector<std::byte> bytes;
    for (int i = 0; i < 20000; ++i) bytes.push_back(static_cast<std::byte>((i / 7) % 31));
    return bytes;
}

}

TEST_CASE("Lz4.BlockRoundTrip") {
    const auto original = Sample();
    const auto packed = Onyx::TestKit::Lz4Compress(original);
    std::vector<std::byte> out(original.size());
    auto result = Lz4Decompress(packed, out);
    REQUIRE(result.ok());
    CHECK(result.value() == original.size());
    CHECK(out == original);
}

TEST_CASE("Lz4.BlockRejectsTruncated") {
    const auto original = Sample();
    auto packed = Onyx::TestKit::Lz4Compress(original);
    packed.resize(packed.size() / 2);
    std::vector<std::byte> out(original.size());
    CHECK(!Lz4Decompress(packed, out).ok());
}

TEST_CASE("Lz4.FrameRoundTrip") {
    const auto original = Sample();
    const auto packed = Onyx::TestKit::Lz4FrameCompress(original);
    auto result = Lz4FrameDecompress(packed);
    REQUIRE(result.ok());
    CHECK(result.value() == original);
}

TEST_CASE("Lz4.FrameRejectsGarbage") {
    const std::vector<std::byte> junk(64, std::byte{0x5A});
    CHECK(!Lz4FrameDecompress(junk).ok());
}

TEST_CASE("Lz4.FrameLargeBlocksRoundTrip") {
    std::vector<std::byte> original;
    unsigned state = 12345;
    while (original.size() < 1536 * 1024) {
        state = state * 1664525u + 1013904223u;
        const size_t run = 400000 + (state >> 16) % 90000;
        original.insert(original.end(), run, static_cast<std::byte>((state >> 8) & 0xFF));
    }
    const auto packed = Onyx::TestKit::Lz4FrameCompress(original, true);
    auto result = Lz4FrameDecompress(packed);
    REQUIRE(result.ok());
    CHECK(result.value() == original);
}

TEST_CASE("Lz4.FrameOutputCapIsEnforced") {
    const auto original = Sample();
    const auto packed = Onyx::TestKit::Lz4FrameCompress(original);
    auto capped = Lz4FrameDecompress(packed, original.size() - 1);
    REQUIRE_FALSE(capped.ok());
    CHECK(capped.error().code == "lz4.tooLarge");
    auto exact = Lz4FrameDecompress(packed, original.size());
    REQUIRE(exact.ok());
    CHECK(exact.value() == original);
}
