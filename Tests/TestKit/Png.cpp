#include <doctest/doctest.h>

#include <Onyx/TestKit/Png.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include "TempDir.h"

using namespace Onyx::TestKit;

TEST_CASE("WritePng then ReadPng round-trips a 3x2 RGBA image") {
    Image8 img;
    img.width = 3;
    img.height = 2;
    for (int i = 0; i < 3 * 2 * 4; ++i) img.rgba.push_back(static_cast<std::byte>((i * 11 + 3) & 0xFF));

    Onyx::Test::TempDir scope("testkit-png");
    std::error_code ec;
    auto path = scope / "roundtrip.png";

    auto written = WritePng(path, img);
    REQUIRE(written.ok());

    auto read = ReadPng(path);
    REQUIRE(read.ok());
    CHECK(read.value().width == 3);
    CHECK(read.value().height == 2);
    CHECK(read.value().rgba == img.rgba);

    std::filesystem::remove(path, ec);
}

TEST_CASE("ReadPng fails for a missing file and WritePng rejects a short buffer") {
    Onyx::Test::TempDir scope("testkit-png");
    CHECK_FALSE(ReadPng(scope / "absent.png").ok());

    Image8 img;
    img.width = 2;
    img.height = 2;
    img.rgba.resize(3);
    CHECK_FALSE(WritePng(scope / "short.png", img).ok());
}
