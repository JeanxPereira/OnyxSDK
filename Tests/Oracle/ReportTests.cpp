#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Report.h"

#include <clocale>
#include <filesystem>
#include <string>
#include <vector>

using namespace Onyx;
using namespace Onyx::Oracle;

namespace {

std::span<const std::byte> Bytes(const char* text, size_t size) {
    return {reinterpret_cast<const std::byte*>(text), size};
}

Render::DrawInfo Draw(std::string name) {
    Render::DrawInfo d;
    d.name = std::move(name);
    return d;
}

struct LocaleGuard {
    std::string saved = [] {
        const char* current = std::setlocale(LC_NUMERIC, nullptr);
        return std::string(current ? current : "C");
    }();
    ~LocaleGuard() { std::setlocale(LC_NUMERIC, saved.c_str()); }
};

}

TEST_CASE("Fnv1a matches the reference vectors") {
    CHECK(Fnv1a({}) == 14695981039346656037ull);
    CHECK(Fnv1a(Bytes("a", 1)) == 12638187200555641996ull);
}

TEST_CASE("FormatFloat prints six decimals and drops the sign of zero") {
    CHECK(FormatFloat(1.0f) == "1.000000");
    CHECK(FormatFloat(0.25f) == "0.250000");
    CHECK(FormatFloat(-1.5f) == "-1.500000");
    CHECK(FormatFloat(0.0f) == "0.000000");
    CHECK(FormatFloat(-0.0f) == "0.000000");
}

TEST_CASE("FormatFloat ignores LC_NUMERIC") {
    LocaleGuard guard;
    if (std::setlocale(LC_NUMERIC, "de-DE") == nullptr) {
        WARN("de-DE locale not available; skipping the locale check");
        return;
    }
    CHECK(FormatFloat(0.25f) == "0.250000");
}

TEST_CASE("BuildReport matches the canonical text byte for byte") {
    Render::DrawInfo d;
    d.name = "quad_additive";
    d.materialIndex = 2;
    d.vertexCount = 100;
    d.triangleCount = 40;
    d.blend = Domain::BlendMode::Additive;
    d.skinned = true;
    d.sky = false;
    d.texturesBound = 2;
    d.jointCount = 12;
    const std::vector<Render::DrawInfo> draws = {d};

    const std::string expected =
        "{\n"
        "  \"scene\": \"test-scene\",\n"
        "  \"width\": 64,\n"
        "  \"height\": 32,\n"
        "  \"pixelHash\": 16045690984503098046,\n"
        "  \"draws\": [\n"
        "    {\n"
        "      \"name\": \"quad_additive\",\n"
        "      \"material\": 2,\n"
        "      \"vertexCount\": 100,\n"
        "      \"triangleCount\": 40,\n"
        "      \"blend\": \"Additive\",\n"
        "      \"skinned\": true,\n"
        "      \"sky\": false,\n"
        "      \"texturesBound\": 2,\n"
        "      \"jointCount\": 12\n"
        "    }\n"
        "  ]\n"
        "}\n";
    const std::string report = BuildReport("test-scene", 64, 32, 0xDEADBEEFCAFEBABEull, draws);
    CHECK(report == expected);
    CHECK(report.find('\r') == std::string::npos);
}

TEST_CASE("BuildReport names every blend mode and handles an empty draw list") {
    std::vector<Render::DrawInfo> draws;
    const char* names[] = {"Opaque", "Alpha", "Additive", "Subtractive", "EnvMap"};
    for (int i = 0; i < 5; ++i) {
        Render::DrawInfo d = Draw("d");
        d.blend = static_cast<Domain::BlendMode>(i);
        draws.push_back(d);
    }
    const std::string report = BuildReport("s", 1, 1, 0, draws);
    for (const char* name : names) {
        CHECK(report.find(std::string("\"blend\": \"") + name + "\"") != std::string::npos);
    }
    const std::string empty = BuildReport("s", 1, 1, 0, {});
    CHECK(empty.find("\"draws\": []\n") != std::string::npos);
}

TEST_CASE("BuildReport is byte identical across calls") {
    const std::vector<Render::DrawInfo> draws = {Draw("solo")};
    CHECK(BuildReport("s", 8, 8, 42, draws) == BuildReport("s", 8, 8, 42, draws));
}

TEST_CASE("BuildReport escapes quotes, backslashes and control characters") {
    const std::vector<Render::DrawInfo> draws = {Draw("a\"b\\c\n")};
    const std::string report = BuildReport("s", 1, 1, 0, draws);
    CHECK(report.find("\"name\": \"a\\\"b\\\\c\\n\"") != std::string::npos);
}

TEST_CASE("JsonEqualMaskingPixelHash accepts identical reports") {
    const std::vector<Render::DrawInfo> draws = {Draw("solo")};
    CHECK(JsonEqualMaskingPixelHash(BuildReport("s", 8, 8, 111, draws), BuildReport("s", 8, 8, 111, draws)));
}

TEST_CASE("JsonEqualMaskingPixelHash masks only the pixelHash value") {
    const std::vector<Render::DrawInfo> draws = {Draw("solo")};
    const std::string a = BuildReport("s", 8, 8, 111, draws);
    const std::string b = BuildReport("s", 8, 8, 222, draws);
    CHECK(a != b);
    CHECK(JsonEqualMaskingPixelHash(a, b));
}

TEST_CASE("JsonEqualMaskingPixelHash catches a real divergence") {
    Render::DrawInfo other = Draw("solo");
    other.vertexCount = 3;
    const std::vector<Render::DrawInfo> a = {Draw("solo")};
    const std::vector<Render::DrawInfo> b = {other};
    CHECK_FALSE(JsonEqualMaskingPixelHash(BuildReport("s", 8, 8, 111, a), BuildReport("s", 8, 8, 111, b)));
}

TEST_CASE("JsonEqualMaskingPixelHash rejects a different line count") {
    const std::vector<Render::DrawInfo> draws = {Draw("solo")};
    CHECK_FALSE(JsonEqualMaskingPixelHash(BuildReport("s", 8, 8, 111, draws), BuildReport("s", 8, 8, 111, {})));
}

TEST_CASE("SameDirectory resolves spelling differences and missing directories") {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "OnyxOracleSameDirectory";
    fs::create_directories(base / "golden");
    CHECK(SameDirectory(base / "golden", base / "golden"));
    CHECK(SameDirectory(base / "golden", base / "golden" / ".." / "golden"));
    CHECK(SameDirectory(base / "golden", base / "golden" / ""));
    CHECK_FALSE(SameDirectory(base / "golden", base / "out"));
    CHECK_FALSE(SameDirectory(base / "golden", base / "golden" / "a"));
    fs::remove_all(base);
}
