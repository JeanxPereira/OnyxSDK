#include <doctest/doctest.h>

#include <Onyx/TestKit/IsoBuilder.h>

#include <cstddef>
#include <cstring>
#include <vector>

using Onyx::TestKit::IsoBuilder;

TEST_CASE("TestKit.IsoBuilderWritesPvdAtSector16") {
    IsoBuilder builder;
    const std::vector<std::byte> data(10, std::byte{1});
    builder.AddFile("A.BIN", data);
    const auto image = builder.Build();
    REQUIRE(image.size() > 16 * 2048 + 6);
    CHECK(image[16 * 2048] == std::byte{1});
    CHECK(std::memcmp(image.data() + 16 * 2048 + 1, "CD001", 5) == 0);
}

TEST_CASE("TestKit.IsoBuilderIsSectorAligned") {
    IsoBuilder builder;
    const std::vector<std::byte> data(3000, std::byte{2});
    builder.AddFile("DIR/SUB/B.BIN", data);
    builder.AddDirectory("EMPTY");
    CHECK(builder.Build().size() % 2048 == 0);
}

namespace {

uint32_t Le32(const std::vector<std::byte>& b, size_t at) {
    uint32_t v = 0;
    for (int i = 3; i >= 0; --i) v = (v << 8) | static_cast<uint32_t>(b[at + i]);
    return v;
}

uint32_t Be32(const std::vector<std::byte>& b, size_t at) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | static_cast<uint32_t>(b[at + i]);
    return v;
}

}

TEST_CASE("TestKit.IsoBuilderWritesBothEndianFieldsAndPathTable") {
    IsoBuilder builder;
    builder.AddFile("DIR/A.BIN", std::vector<std::byte>(100, std::byte{7}));
    const auto image = builder.Build();
    const size_t pvd = 16 * 2048;

    const uint32_t volumeLe = Le32(image, pvd + 80);
    CHECK(volumeLe == Be32(image, pvd + 84));
    CHECK(volumeLe * 2048 == image.size());

    const uint32_t rootLe = Le32(image, pvd + 156 + 2);
    CHECK(rootLe == Be32(image, pvd + 156 + 6));
    const size_t rootAt = static_cast<size_t>(rootLe) * 2048;
    REQUIRE(rootAt + 34 <= image.size());
    CHECK(image[rootAt + 25] == std::byte{2});
    CHECK(Le32(image, rootAt + 2) == rootLe);
    CHECK(image[rootAt + 33] == std::byte{0});

    const uint32_t tableL = Le32(image, pvd + 140);
    const uint32_t tableM = Be32(image, pvd + 148);
    REQUIRE(tableL * 2048 + 10 <= image.size());
    CHECK(Le32(image, tableL * 2048 + 2) == rootLe);
    CHECK(Be32(image, tableM * 2048 + 2) == rootLe);
    CHECK(Le32(image, pvd + 132) == Be32(image, pvd + 136));
}
