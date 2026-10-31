#include <doctest/doctest.h>

#include <Onyx/TestKit/SceneFixtures.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

using namespace Onyx;
using namespace Onyx::TestKit;
using Domain::BlendMode;
using Domain::TextureRole;

namespace {

std::array<uint8_t, 4> PixelAt(const Domain::Texture& tex, uint32_t x, uint32_t y) {
    size_t idx = (static_cast<size_t>(y) * tex.width + x) * 4;
    return {static_cast<uint8_t>(tex.pixels[idx]), static_cast<uint8_t>(tex.pixels[idx + 1]),
            static_cast<uint8_t>(tex.pixels[idx + 2]), static_cast<uint8_t>(tex.pixels[idx + 3])};
}

}

TEST_CASE("MakeChecker produces the requested size and pixel buffer") {
    auto tex = MakeChecker(16, {255, 0, 0, 255}, {0, 255, 0, 255}, "checker16");
    CHECK(tex.name == "checker16");
    CHECK(tex.width == 16);
    CHECK(tex.height == 16);
    CHECK(tex.format == Domain::PixelFormat::Rgba8);
    CHECK(tex.pixels.size() == 16u * 16u * 4u);
    CHECK(tex.IsValid());
}

TEST_CASE("MakeChecker alternates colours on an 8 px grid") {
    std::array<uint8_t, 4> a{255, 0, 0, 255};
    std::array<uint8_t, 4> b{0, 255, 0, 255};
    auto tex = MakeChecker(16, a, b, "checker16");
    CHECK(PixelAt(tex, 0, 0) == a);
    CHECK(PixelAt(tex, 8, 0) == b);
    CHECK(PixelAt(tex, 8, 8) == a);
}

TEST_CASE("MakeGradient interpolates top row to bottom row") {
    std::array<uint8_t, 4> top{255, 255, 255, 255};
    std::array<uint8_t, 4> bottom{0, 0, 0, 255};
    auto tex = MakeGradient(16, top, bottom, "gradient16");
    CHECK(tex.width == 16);
    CHECK(tex.pixels.size() == 16u * 16u * 4u);
    CHECK(PixelAt(tex, 0, 0) == top);
    CHECK(PixelAt(tex, 0, 15) == bottom);
}

TEST_CASE("MakeSolid fills every texel with the requested colour") {
    std::array<uint8_t, 4> c{10, 20, 30, 40};
    auto tex = MakeSolid(8, c, "solid8");
    CHECK(tex.pixels.size() == 8u * 8u * 4u);
    for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 8; ++x) CHECK(PixelAt(tex, x, y) == c);
    }
}

TEST_CASE("MakeBumpNormal decodes to unit-length normals with z>0") {
    auto tex = MakeBumpNormal(16, "bump16");
    CHECK(tex.pixels.size() == 16u * 16u * 4u);
    for (uint32_t y = 0; y < tex.height; ++y) {
        for (uint32_t x = 0; x < tex.width; ++x) {
            auto px = PixelAt(tex, x, y);
            float nx = static_cast<float>(px[0]) / 255.0f * 2.0f - 1.0f;
            float ny = static_cast<float>(px[1]) / 255.0f * 2.0f - 1.0f;
            float nz = static_cast<float>(px[2]) / 255.0f * 2.0f - 1.0f;
            CHECK(std::fabs(std::sqrt(nx * nx + ny * ny + nz * nz) - 1.0f) < 0.02f);
            CHECK(nz > 0.0f);
        }
    }
}

TEST_CASE("Texture generators are deterministic across repeated calls") {
    std::array<uint8_t, 4> a{255, 0, 0, 255};
    std::array<uint8_t, 4> b{0, 255, 0, 255};
    CHECK(MakeChecker(16, a, b, "c").pixels == MakeChecker(16, a, b, "c").pixels);
    CHECK(MakeGradient(16, a, b, "g").pixels == MakeGradient(16, a, b, "g").pixels);
    CHECK(MakeSolid(16, a, "s").pixels == MakeSolid(16, a, "s").pixels);
    CHECK(MakeBumpNormal(16, "n").pixels == MakeBumpNormal(16, "n").pixels);
}

TEST_CASE("BuildCorpus returns the five scenes in canonical order") {
    auto corpus = BuildCorpus();
    REQUIRE(corpus.size() == 5);
    CHECK(corpus[0].name == "sphere-grid");
    CHECK(corpus[1].name == "sphere-grid-textured");
    CHECK(corpus[2].name == "skinned-cube");
    CHECK(corpus[3].name == "blend-stack");
    CHECK(corpus[4].name == "joint-chain-200");
    for (size_t i = 0; i < corpus.size(); ++i) CHECK(corpus[i].textured == (i == 1));
    for (const auto& f : corpus) {
        CHECK(f.width == 512);
        CHECK(f.height == 512);
        CHECK(f.fovYDegrees == doctest::Approx(45.0f));
    }
}

TEST_CASE("sphere-grid has 9 parts/materials and a 10-entry texture pool") {
    auto f = BuildSphereGrid();
    CHECK(f.scene.parts.size() == 9);
    CHECK(f.scene.materials.size() == 9);
    CHECK(f.scene.textures.size() == 10);
}

TEST_CASE("sphere-grid every material binds all 9 roles in range") {
    auto f = BuildSphereGrid();
    const TextureRole roles[] = {TextureRole::Diffuse, TextureRole::Normal,   TextureRole::Occlusion,
                                 TextureRole::Gloss,   TextureRole::Height,   TextureRole::Scatter,
                                 TextureRole::Detail,  TextureRole::Emissive, TextureRole::EnvMap};
    for (const auto& mat : f.scene.materials) {
        CHECK(mat.textures.size() == 9);
        for (auto role : roles) {
            auto it = mat.textures.find(role);
            REQUIRE(it != mat.textures.end());
            CHECK(it->second < f.scene.textures.size());
        }
    }
}

TEST_CASE("sphere-grid metallic is {0, 0.5, 1} by column") {
    auto f = BuildSphereGrid();
    REQUIRE(f.scene.materials.size() == 9);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            CHECK(f.scene.materials[row * 3 + col].metallic == doctest::Approx(float(col) * 0.5f));
        }
    }
}

TEST_CASE("skinned-cube has a skeleton and every vertex's weights sum to 1") {
    auto f = BuildSkinnedCube();
    REQUIRE(static_cast<bool>(f.scene.skeleton));
    CHECK(f.scene.skeleton->joints.size() == 3);
    REQUIRE(f.scene.parts.size() == 1);
    const auto& part = f.scene.parts[0];
    REQUIRE(part.weights.size() == part.positions.size());
    REQUIRE(part.joints.size() == part.positions.size());
    for (const auto& w : part.weights) {
        CHECK(w.x + w.y + w.z + w.w == doctest::Approx(1.0f).epsilon(1e-4));
    }
}

TEST_CASE("blend-stack has exactly one Additive and one Subtractive material") {
    auto f = BuildBlendStack();
    int additive = 0, subtractive = 0;
    for (const auto& mat : f.scene.materials) {
        if (mat.blend == BlendMode::Additive) ++additive;
        if (mat.blend == BlendMode::Subtractive) ++subtractive;
    }
    CHECK(additive == 1);
    CHECK(subtractive == 1);
}

TEST_CASE("joint-chain-200 has 200 joints and 200 parts") {
    auto f = BuildJointChain200();
    REQUIRE(static_cast<bool>(f.scene.skeleton));
    CHECK(f.scene.skeleton->joints.size() == 200);
    CHECK(f.scene.parts.size() == 200);
}

TEST_CASE("BuildCorpus is deterministic across repeated calls") {
    auto a = BuildCorpus();
    auto b = BuildCorpus();
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].scene.parts[0].positions == b[i].scene.parts[0].positions);
        CHECK(a[i].scene.parts[0].indices == b[i].scene.parts[0].indices);
    }
}

TEST_CASE("Every corpus material carries the v1 colour scale and normal encoding") {
    auto corpus = BuildCorpus();
    corpus.push_back(BuildAnimatedChain());
    for (const auto& f : corpus) {
        REQUIRE_FALSE(f.scene.materials.empty());
        for (const auto& mat : f.scene.materials) {
            CHECK(mat.vertexColorScale == 2.0f);
            CHECK(mat.normalEncoding == Domain::NormalEncoding::ReconstructZ);
        }
    }
}

TEST_CASE("BuildAnimatedChain has one 1 s clip with keys on joints 1 and 2") {
    auto f = BuildAnimatedChain();
    REQUIRE(static_cast<bool>(f.scene.animations));
    REQUIRE(f.scene.animations->clips.size() == 1);
    const auto& clip = f.scene.animations->clips[0];
    CHECK(clip.name == "bend");
    CHECK(clip.duration == doctest::Approx(1.0f));
    REQUIRE(clip.channels.size() == 2);
    CHECK(clip.channels[0].joint == 1);
    CHECK(clip.channels[1].joint == 2);
    for (const auto& ch : clip.channels) {
        REQUIRE(ch.rotation.size() == 3);
        CHECK(ch.rotation.front().time == doctest::Approx(0.0f));
        CHECK(ch.rotation.back().time == doctest::Approx(1.0f));
    }
}
