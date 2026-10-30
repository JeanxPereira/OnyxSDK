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
