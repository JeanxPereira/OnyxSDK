// ── OnyxOracle corpus texture generator tests (doctest) ───────────────────
//
// CorpusTextures is a pure (GL-free) helper library -- no GL calls, no file
// I/O -- so it can be exercised here without a GL context or the onyx-oracle
// executable. This test target compiles CorpusTextures.cpp directly.

#include <doctest/doctest.h>

#include <CorpusScenes.h>
#include <CorpusTextures.h>
#include <RenderReport.h>

#include <clocale>
#include <cmath>
#include <cstring>

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

// ── OnyxOracle corpus scene builder tests (doctest) ────────────────────────
//
// CorpusScenes is also pure (GL-free) -- these are shape-level assertions
// only: part/material/joint counts, in-range role indices, skin weight
// sums, and byte-for-byte determinism. No GL context, no rendering.

using Onyx::Parsers::BlendMode;
using Onyx::Parsers::TextureRole;

TEST_CASE("OracleCorpus: BuildCorpus returns the four scenes in canonical order") {
    auto corpus = BuildCorpus();

    REQUIRE(corpus.size() == 4);
    CHECK(corpus[0].name == "sphere-grid");
    CHECK(corpus[1].name == "skinned-cube");
    CHECK(corpus[2].name == "blend-stack");
    CHECK(corpus[3].name == "joint-chain-200");
}

TEST_CASE("OracleCorpus: sphere-grid has 9 parts/materials and a 10-entry texture pool") {
    auto cs = BuildSphereGrid();
    const auto& scene = cs.scene;

    CHECK(scene.meshParts.size() == 9);
    CHECK(scene.materials.size() == 9);
    CHECK(scene.textures.size() == 10);
}

TEST_CASE("OracleCorpus: sphere-grid every material binds all 9 roles in-range") {
    auto cs = BuildSphereGrid();
    const auto& scene = cs.scene;

    static const TextureRole kAllRoles[] = {
        TextureRole::Diffuse, TextureRole::Normal,   TextureRole::Occlusion,
        TextureRole::Gloss,   TextureRole::Height,   TextureRole::Scatter,
        TextureRole::Detail,  TextureRole::Emissive, TextureRole::EnvMap,
    };

    for (const auto& mat : scene.materials) {
        CHECK(mat.textures.size() == 9);
        for (auto role : kAllRoles) {
            auto it = mat.textures.find(role);
            REQUIRE(it != mat.textures.end());
            CHECK(it->second >= 0);
            CHECK(it->second < (int)scene.textures.size());
        }
    }
}

TEST_CASE("OracleCorpus: sphere-grid metallic is {0, 0.5, 1} by column") {
    auto cs = BuildSphereGrid();
    const auto& scene = cs.scene;

    REQUIRE(scene.materials.size() == 9);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            float expected = float(col) * 0.5f;
            CHECK(scene.materials[row * 3 + col].metallic == doctest::Approx(expected));
        }
    }
}

TEST_CASE("OracleCorpus: skinned-cube has a skeleton and every vertex's weights sum to 1") {
    auto cs = BuildSkinnedCube();
    const auto& scene = cs.scene;

    REQUIRE(scene.HasSkeleton());
    REQUIRE(scene.meshParts.size() == 1);

    for (const auto& v : scene.meshParts[0].vertices) {
        float sum = v.boneWeights.x + v.boneWeights.y + v.boneWeights.z + v.boneWeights.w;
        CHECK(sum == doctest::Approx(1.0f).epsilon(1e-4));
    }
}

TEST_CASE("OracleCorpus: blend-stack has exactly one Additive and one Subtractive material") {
    auto cs = BuildBlendStack();
    const auto& scene = cs.scene;

    int additive = 0, subtractive = 0;
    for (const auto& mat : scene.materials) {
        if (mat.blendMode == BlendMode::Additive) ++additive;
        if (mat.blendMode == BlendMode::Subtractive) ++subtractive;
    }
    CHECK(additive == 1);
    CHECK(subtractive == 1);
}

TEST_CASE("OracleCorpus: joint-chain-200 has 200 joints and 200 parts") {
    auto cs = BuildJointChain200();
    const auto& scene = cs.scene;

    REQUIRE(scene.HasSkeleton());
    CHECK(scene.skeleton->joints.size() == 200);
    CHECK(scene.meshParts.size() == 200);
}

TEST_CASE("OracleCorpus: BuildCorpus is deterministic across repeated calls") {
    auto corpus1 = BuildCorpus();
    auto corpus2 = BuildCorpus();

    REQUIRE(corpus1.size() == corpus2.size());
    for (size_t i = 0; i < corpus1.size(); ++i) {
        const auto& v1 = corpus1[i].scene.meshParts[0].vertices;
        const auto& v2 = corpus2[i].scene.meshParts[0].vertices;
        REQUIRE(v1.size() == v2.size());
        CHECK(std::memcmp(v1.data(), v2.data(), v1.size() * sizeof(v1[0])) == 0);
    }
}

// ── RenderReport tests (doctest) ───────────────────────────────────────────
//
// RenderReport is also pure (GL-free): Fnv1a/FormatFloat/BuildReport only
// touch plain data (RenderBatch's non-GL fields; gpuMesh/GL ids are never
// dereferenced, just compared to zero), so this exercises the exact string
// BuildReport produces without a GL context. The BuildReport test below IS
// the format spec: any future change to the report layout must update this
// verbatim string deliberately, not accidentally.

using Onyx::Rendering::RenderBatch;

TEST_CASE("OracleCorpus: Fnv1a matches the reference vectors for empty and \"a\"") {
    CHECK(Fnv1a("", 0) == 14695981039346656037ull);
    CHECK(Fnv1a("a", 1) == 0xaf63dc4c8601ec8cull);
}

TEST_CASE("OracleCorpus: FormatFloat renders six decimals and normalizes negative zero") {
    CHECK(FormatFloat(1.0f) == "1.000000");
    CHECK(FormatFloat(-0.0f) == "0.000000");
    CHECK(FormatFloat(0.0f) == "0.000000");
    CHECK(FormatFloat(-1.5f) == "-1.500000");
}

TEST_CASE("OracleCorpus: BuildReport matches the canonical report format byte-for-byte") {
    RenderBatch alpha;
    alpha.name = "batch_alpha";
    alpha.vertexCount = 100;
    alpha.triangleCount = 40;
    alpha.blendMode = BlendMode::Additive;
    alpha.hasTexture = true;
    alpha.hasEnvmap = false;
    alpha.hasSkeleton = true;
    alpha.metallic = 0.25f;
    alpha.materialColor[0] = 0.1f;
    alpha.materialColor[1] = 0.2f;
    alpha.materialColor[2] = 0.3f;
    alpha.materialColor[3] = 0.4f;
    alpha.texture0 = 5;    // bound
    alpha.texture1 = 0;    // unbound
    alpha.texNormal = 7;   // bound
    alpha.texAO = 0;       // unbound
    alpha.texGloss = 0;    // unbound
    alpha.texScatter = 0;  // unbound -> roleTexturesBound == 2

    RenderBatch beta;
    beta.name = "batch_beta";
    beta.metallic = -0.0f; // exercises the FormatFloat -0 normalization end-to-end
    // Everything else (vertexCount/triangleCount/blendMode/hasTexture/
    // hasEnvmap/hasSkeleton/materialColor/all texture ids) stays at
    // RenderBatch's own defaults: 0, 0, Normal, false, false, false,
    // {1,1,1,1}, 0 -> roleTexturesBound == 0.

    std::vector<RenderBatch> batches = {alpha, beta};
    std::vector<size_t> paletteJointCounts = {12, 0};

    std::string report = BuildReport("test-scene", 64, 32, 0xDEADBEEFCAFEBABEull,
                                      batches, paletteJointCounts);

    const std::string expected =
        "{\n"
        "  \"scene\": \"test-scene\",\n"
        "  \"width\": 64,\n"
        "  \"height\": 32,\n"
        "  \"pixelHash\": 16045690984503098046,\n"
        "  \"batches\": [\n"
        "    {\n"
        "      \"name\": \"batch_alpha\",\n"
        "      \"vertexCount\": 100,\n"
        "      \"triangleCount\": 40,\n"
        "      \"blendMode\": \"Additive\",\n"
        "      \"hasTexture\": true,\n"
        "      \"hasEnvmap\": false,\n"
        "      \"hasSkeleton\": true,\n"
        "      \"metallic\": 0.250000,\n"
        "      \"materialColor\": [0.100000, 0.200000, 0.300000, 0.400000],\n"
        "      \"roleTexturesBound\": 2,\n"
        "      \"paletteJointCount\": 12\n"
        "    },\n"
        "    {\n"
        "      \"name\": \"batch_beta\",\n"
        "      \"vertexCount\": 0,\n"
        "      \"triangleCount\": 0,\n"
        "      \"blendMode\": \"Normal\",\n"
        "      \"hasTexture\": false,\n"
        "      \"hasEnvmap\": false,\n"
        "      \"hasSkeleton\": false,\n"
        "      \"metallic\": 0.000000,\n"
        "      \"materialColor\": [1.000000, 1.000000, 1.000000, 1.000000],\n"
        "      \"roleTexturesBound\": 0,\n"
        "      \"paletteJointCount\": 0\n"
        "    }\n"
        "  ]\n"
        "}\n";

    CHECK(report == expected);
    CHECK(report.find('\r') == std::string::npos);
}

TEST_CASE("OracleCorpus: BuildReport is byte-identical across repeated calls") {
    RenderBatch batch;
    batch.name = "solo";
    batch.vertexCount = 3;
    batch.triangleCount = 1;
    std::vector<RenderBatch> batches = {batch};
    std::vector<size_t> paletteJointCounts = {0};

    std::string report1 = BuildReport("solo-scene", 8, 8, 42ull, batches, paletteJointCounts);
    std::string report2 = BuildReport("solo-scene", 8, 8, 42ull, batches, paletteJointCounts);

    CHECK(report1 == report2);
}

namespace {

// Restores whatever LC_NUMERIC was in effect before the test flipped it,
// on every exit path (normal return, early return on a missing locale, or
// a failed REQUIRE unwinding the stack) -- so a locale change here can
// never leak into any other test case.
struct LocaleNumericGuard {
    std::string saved = [] {
        const char* cur = std::setlocale(LC_NUMERIC, nullptr);
        return std::string(cur ? cur : "C");
    }();
    ~LocaleNumericGuard() { std::setlocale(LC_NUMERIC, saved.c_str()); }
};

} // namespace

TEST_CASE("OracleCorpus: FormatFloat stays locale-independent under LC_NUMERIC de-DE") {
    LocaleNumericGuard guard;

    if (std::setlocale(LC_NUMERIC, "de-DE") == nullptr) {
        // de-DE isn't installed on this machine/CI image. Can't exercise
        // the comma-decimal failure mode here, but a missing locale isn't
        // itself a bug -- warn instead of failing the suite.
        WARN("de-DE locale not available; skipping locale-independence check");
        return;
    }

    // std::to_chars must ignore the flipped LC_NUMERIC entirely: dot, not
    // comma, exactly like FormatFloat's other tests expect under "C".
    CHECK(FormatFloat(0.25f) == "0.250000");
    CHECK(FormatFloat(-0.0f) == "0.000000");

    // Re-run the metallic/materialColor lines from the verbatim BuildReport
    // case above under the flipped locale: if FormatFloat ever regressed to
    // a locale-sensitive formatter, these would start containing commas.
    RenderBatch alpha;
    alpha.name = "batch_alpha";
    alpha.metallic = 0.25f;
    alpha.materialColor[0] = 0.1f;
    alpha.materialColor[1] = 0.2f;
    alpha.materialColor[2] = 0.3f;
    alpha.materialColor[3] = 0.4f;
    std::vector<RenderBatch> batches = {alpha};
    std::vector<size_t> paletteJointCounts = {0};

    std::string report = BuildReport("test-scene", 64, 32, 0ull, batches, paletteJointCounts);
    CHECK(report.find("\"metallic\": 0.250000,\n") != std::string::npos);
    CHECK(report.find("\"materialColor\": [0.100000, 0.200000, 0.300000, 0.400000],\n") !=
          std::string::npos);
}
