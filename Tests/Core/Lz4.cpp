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
