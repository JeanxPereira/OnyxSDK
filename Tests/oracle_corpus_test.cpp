// ── OnyxOracle corpus texture generator tests (doctest) ───────────────────
//
// CorpusTextures is a pure (GL-free) helper library -- no GL calls, no file
// I/O -- so it can be exercised here without a GL context or the onyx-oracle
// executable. This test target compiles CorpusTextures.cpp directly.

#include <doctest/doctest.h>

#include <CorpusTextures.h>

#include <cmath>

using namespace Onyx::OracleTool;
using Onyx::Parsers::TextureData;

namespace {

std::array<uint8_t, 4> PixelAt(const TextureData& tex, uint32_t x, uint32_t y) {
    size_t idx = (static_cast<size_t>(y) * tex.width + x) * 4;
    return {tex.pixels[idx + 0], tex.pixels[idx + 1], tex.pixels[idx + 2], tex.pixels[idx + 3]};
}

} // namespace

TEST_CASE("OracleCorpus: MakeChecker produces the requested size and pixel buffer") {
    std::array<uint8_t, 4> a{255, 0, 0, 255};
    std::array<uint8_t, 4> b{0, 255, 0, 255};

    auto tex = MakeChecker(16, a, b, "checker16");

    REQUIRE(tex != nullptr);
    CHECK(tex->name == "checker16");
    CHECK(tex->width == 16);
    CHECK(tex->height == 16);
    CHECK(tex->pixels.size() == 16u * 16u * 4u);
    CHECK(tex->isCompressed == false);
}

TEST_CASE("OracleCorpus: MakeChecker alternates colorA/colorB on an 8-px grid") {
    std::array<uint8_t, 4> a{255, 0, 0, 255};
    std::array<uint8_t, 4> b{0, 255, 0, 255};

    auto tex = MakeChecker(16, a, b, "checker16");

    CHECK(PixelAt(*tex, 0, 0) == a);
    CHECK(PixelAt(*tex, 8, 0) == b);
}

TEST_CASE("OracleCorpus: MakeGradient interpolates top row to bottom row") {
    std::array<uint8_t, 4> top{255, 255, 255, 255};
    std::array<uint8_t, 4> bottom{0, 0, 0, 255};

    auto tex = MakeGradient(16, top, bottom, "gradient16");

    REQUIRE(tex != nullptr);
    CHECK(tex->width == 16);
    CHECK(tex->height == 16);
    CHECK(tex->pixels.size() == 16u * 16u * 4u);
    CHECK(PixelAt(*tex, 0, 0) == top);
    CHECK(PixelAt(*tex, 0, 15) == bottom);
}

TEST_CASE("OracleCorpus: MakeSolid fills every texel with the requested color") {
    std::array<uint8_t, 4> c{10, 20, 30, 40};

    auto tex = MakeSolid(8, c, "solid8");

    REQUIRE(tex != nullptr);
    CHECK(tex->width == 8);
    CHECK(tex->height == 8);
    CHECK(tex->pixels.size() == 8u * 8u * 4u);
    for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 8; ++x) {
            CHECK(PixelAt(*tex, x, y) == c);
        }
    }
}

TEST_CASE("OracleCorpus: MakeBumpNormal decodes to unit-length normals with z>0") {
    auto tex = MakeBumpNormal(16, "bump16");

    REQUIRE(tex != nullptr);
    CHECK(tex->width == 16);
    CHECK(tex->height == 16);
    CHECK(tex->pixels.size() == 16u * 16u * 4u);

    for (uint32_t y = 0; y < tex->height; ++y) {
        for (uint32_t x = 0; x < tex->width; ++x) {
            auto px = PixelAt(*tex, x, y);
            float nx = static_cast<float>(px[0]) / 255.0f * 2.0f - 1.0f;
            float ny = static_cast<float>(px[1]) / 255.0f * 2.0f - 1.0f;
            float nz = static_cast<float>(px[2]) / 255.0f * 2.0f - 1.0f;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            CHECK(std::fabs(len - 1.0f) < 0.02f);
            CHECK(nz > 0.0f);
        }
    }
}

TEST_CASE("OracleCorpus: generators are deterministic across repeated calls") {
    std::array<uint8_t, 4> a{255, 0, 0, 255};
    std::array<uint8_t, 4> b{0, 255, 0, 255};

    auto checker1 = MakeChecker(16, a, b, "checker16");
    auto checker2 = MakeChecker(16, a, b, "checker16");
    CHECK(checker1->pixels == checker2->pixels);

    auto gradient1 = MakeGradient(16, a, b, "gradient16");
    auto gradient2 = MakeGradient(16, a, b, "gradient16");
    CHECK(gradient1->pixels == gradient2->pixels);

    auto solid1 = MakeSolid(16, a, "solid16");
    auto solid2 = MakeSolid(16, a, "solid16");
    CHECK(solid1->pixels == solid2->pixels);

    auto bump1 = MakeBumpNormal(16, "bump16");
    auto bump2 = MakeBumpNormal(16, "bump16");
    CHECK(bump1->pixels == bump2->pixels);
}
