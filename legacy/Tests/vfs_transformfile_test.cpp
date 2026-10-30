#include <doctest/doctest.h>

#include <memory>
#include <vector>
#include <cstdint>

#include <Onyx/Vfs/MemoryFile.h>
#include <Onyx/Vfs/TransformFile.h>

using Onyx::Vfs::MemoryFile;
using Onyx::Vfs::MakeXorFile;

TEST_CASE("TransformFile XOR round-trips bytes") {
    std::vector<uint8_t> bytes = {0x01, 0x02, 0x03, 0xFF};
    auto inner = std::make_shared<MemoryFile>(bytes);
    auto xored = MakeXorFile(inner, 0x69);

    std::vector<uint8_t> got = xored->ReadAll();
    REQUIRE(got.size() == bytes.size());
    for (size_t i = 0; i < bytes.size(); ++i)
        CHECK(got[i] == static_cast<uint8_t>(bytes[i] ^ 0x69));
}

TEST_CASE("MakeXorFile with key 0 is identity (returns inner)") {
    std::vector<uint8_t> bytes = {0xAA, 0xBB, 0xCC};
    auto inner = std::make_shared<MemoryFile>(bytes);
    auto same  = MakeXorFile(inner, 0x00);

    CHECK(same.get() == inner.get());        // no wrapping
    CHECK(same->ReadAll() == bytes);
}
