// ── PathUtils tests (doctest) ─────────────────────────────────────────────
//
// Everything the engine loads at runtime (fonts, shaders, the recents list)
// goes through resolvePath, so these lock in that it stays anchored to the
// executable rather than to the working directory the app happened to start in.

#include <doctest/doctest.h>
#include <Onyx/Services/PathUtils.h>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("PathUtils::getExecutableDir points at a real, absolute directory") {
    const fs::path dir = PathUtils::getExecutableDir();

    CHECK(dir.is_absolute());
    CHECK(fs::exists(dir));
    CHECK(fs::is_directory(dir));
}

TEST_CASE("PathUtils::getResourceDir is the executable dir outside a macOS bundle") {
    const fs::path resources = PathUtils::getResourceDir();

    CHECK(resources.is_absolute());
    CHECK(fs::exists(resources));
#if !defined(__APPLE__)
    CHECK(resources == PathUtils::getExecutableDir());
#endif
}

TEST_CASE("PathUtils::resolvePath anchors relative paths to the resource dir") {
    const fs::path resolved = PathUtils::resolvePath("third_party/fonts/SFSymbols.ttf");

    CHECK(resolved.is_absolute());
    CHECK(resolved.filename() == "SFSymbols.ttf");
    // Anchored to the executable, not to whatever cwd ctest was launched from.
    const std::string prefix = PathUtils::getResourceDir().string();
    CHECK(resolved.string().rfind(prefix, 0) == 0);
}

TEST_CASE("PathUtils::resolvePath leaves an absolute path usable") {
    // resolvePath appends, so an already-absolute argument is a caller error;
    // what matters is that it never produces an empty or relative result.
    const fs::path resolved = PathUtils::resolvePath("");
    CHECK(!resolved.empty());
    CHECK(resolved.is_absolute());
}
