#include <doctest/doctest.h>

#include <Onyx/Core/Services/Paths.h>

#include <cstdlib>
#include <filesystem>
#include <string>

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

#ifdef _WIN32
TEST_CASE("Paths.IsInsideIsCaseInsensitiveOnWindows") {
    CHECK(Paths::IsInside("C:/a", "c:\\a\\b"));
    CHECK_FALSE(Paths::IsInside("C:/a", "c:\\a\\..\\b"));
}
#endif

TEST_CASE("Paths.UserConfigNeverRelative") {
#ifdef _WIN32
    const char* name = "APPDATA";
#else
    const char* name = "XDG_CONFIG_HOME";
#endif
    const char* old = std::getenv(name);
    std::string saved = old ? old : "";
#ifdef _WIN32
    _putenv_s(name, "");
#else
    setenv(name, "", 1);
#endif
    auto p = Paths::UserConfig("OnyxTest");
    CHECK((p.empty() || p.is_absolute()));
#ifdef _WIN32
    _putenv_s(name, saved.c_str());
#else
    setenv(name, saved.c_str(), 1);
#endif
}

TEST_CASE("Paths::FromUtf8 round-trips a non-ASCII name") {
    const std::string name = reinterpret_cast<const char*>(u8"caf\u00e9-\u00fc");
    const fs::path path = Paths::FromUtf8(name);
    CHECK(Paths::ToUtf8(path) == name);
    CHECK(path.u8string() == std::u8string(u8"caf\u00e9-\u00fc"));
}
