#include <doctest/doctest.h>

#include <Onyx/TestKit/Png.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>

using namespace Onyx::TestKit;

TEST_CASE("WritePng then ReadPng round-trips a 3x2 RGBA image") {
    Image8 img;
    img.width = 3;
    img.height = 2;
    for (int i = 0; i < 3 * 2 * 4; ++i) img.rgba.push_back(static_cast<std::byte>((i * 11 + 3) & 0xFF));

    auto dir = std::filesystem::temp_directory_path() / "onyx-testkit-png";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto path = dir / "roundtrip.png";

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
    auto dir = std::filesystem::temp_directory_path() / "onyx-testkit-png";
    CHECK_FALSE(ReadPng(dir / "absent.png").ok());

    Image8 img;
    img.width = 2;
    img.height = 2;
    img.rgba.resize(3);
    CHECK_FALSE(WritePng(dir / "short.png", img).ok());
}
