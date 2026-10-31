#include <doctest/doctest.h>

#include <Onyx/Core/Domain/Texture.h>
#include <Onyx/Exchange/Png.h>
#include <Onyx/TestKit/Png.h>
#include <Onyx/TestKit/SceneFixtures.h>

#include "Support.h"

#include <cstring>

using namespace Onyx;

TEST_CASE("WritePng round-trips through TestKit::ReadPng") {
    Tests::TempDir dir("png_roundtrip");
    const Domain::Texture checker = TestKit::MakeChecker(8, {255, 0, 0, 255}, {0, 0, 255, 128}, "checker");
    const auto path = dir.path / "checker.png";

    const Result<void> written = Exchange::WritePng(checker, path);
    REQUIRE(written.ok());

    const auto read = TestKit::ReadPng(path);
    REQUIRE(read.ok());
    CHECK(read.value().width == 8);
    CHECK(read.value().height == 8);
    REQUIRE(read.value().rgba.size() == checker.pixels.size());
    CHECK(std::memcmp(read.value().rgba.data(), checker.pixels.data(), checker.pixels.size()) == 0);
}

TEST_CASE("WritePng refuses a Bc1 texture") {
    Tests::TempDir dir("png_bc1");
    Domain::Texture texture;
    texture.width = 4;
    texture.height = 4;
    texture.format = Domain::PixelFormat::Bc1;
    texture.pixels.assign(8, std::byte{0});

    const Result<void> written = Exchange::WritePng(texture, dir.path / "bc1.png");
    REQUIRE_FALSE(written.ok());
    CHECK(written.error().code == "exchange.png.format");
}
