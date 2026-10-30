#include <doctest/doctest.h>

#include <Onyx/Core/Services/Paths.h>

#include <filesystem>

using namespace Onyx::Services;
namespace fs = std::filesystem;

TEST_CASE("Paths.NormalizeCollapsesDotSegments") {
    auto p = Paths::Normalize(fs::path("a") / "." / "b" / ".." / "c");
    CHECK(p.generic_string().find("a/c") != std::string::npos);
    CHECK(p.generic_string().find("..") == std::string::npos);
    CHECK(p.generic_string().find("/./") == std::string::npos);
}

TEST_CASE("Paths.IsInsideRejectsTraversal") {
    fs::path base = fs::temp_directory_path() / "base";
    CHECK(Paths::IsInside(base, base / "x" / "y"));
    CHECK_FALSE(Paths::IsInside(base, base / ".." / "x"));
    CHECK_FALSE(Paths::IsInside(base, fs::temp_directory_path() / "baseline"));
}

TEST_CASE("Paths.UserConfigEndsWithAppName") {
    auto p = Paths::UserConfig("OnyxTest");
    CHECK(p.filename() == "OnyxTest");
}
