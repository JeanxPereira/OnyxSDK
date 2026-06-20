#include <doctest/doctest.h>
#include <Onyx/Services/AppConfig.h>

#include <cstring>
#include <filesystem>
#include <string>

using namespace Onyx::Services;

TEST_CASE("AppConfig saves and loads via TOML round-trip") {
    AppConfig a;
    a.windowX = 50; a.windowY = 60; a.windowW = 1600; a.windowH = 900;
    a.maximized = true;
    a.uiScale = 1.5f; a.fontSize = 18.0f; a.fontPath = "fonts/custom.otf";
    a.nativeDecorations = false; a.nativeMenuBar = false;
    a.accentR = 0.10f; a.accentG = 0.20f; a.accentB = 0.30f; a.accentA = 0.40f;
    a.themeMode = 2;
    a.audioVolume = 0.7f;
    a.bgTopR = 0.11f; a.gridA = 0.66f; a.hlR = 0.5f;
    a.camFov = 60.0f; a.camAutoNear = false; a.camPanelVisible = true;
    a.camManualFar = 1234.0f;

    AppConfig::CustomPreset p{};
    std::strncpy(p.name, "MyPreset", sizeof(p.name) - 1);
    p.r = 0.21f; p.g = 0.32f; p.b = 0.43f;
    a.customPresets.push_back(p);

    const auto path =
        (std::filesystem::temp_directory_path() / "onyx_appconfig_roundtrip.toml").string();
    a.save(path);

    AppConfig b = AppConfig::load(path);

    CHECK(b.windowX == 50);
    CHECK(b.windowW == 1600);
    CHECK(b.windowH == 900);
    CHECK(b.maximized == true);
    CHECK(b.uiScale == doctest::Approx(1.5f));
    CHECK(b.fontSize == doctest::Approx(18.0f));
    CHECK(b.fontPath == "fonts/custom.otf");
    CHECK(b.nativeDecorations == false);
    CHECK(b.nativeMenuBar == false);
    CHECK(b.accentR == doctest::Approx(0.10f));
    CHECK(b.accentA == doctest::Approx(0.40f));
    CHECK(static_cast<int>(b.themeMode) == 2);
    CHECK(b.audioVolume == doctest::Approx(0.7f));
    CHECK(b.bgTopR == doctest::Approx(0.11f));
    CHECK(b.gridA == doctest::Approx(0.66f));
    CHECK(b.camFov == doctest::Approx(60.0f));
    CHECK(b.camAutoNear == false);
    CHECK(b.camPanelVisible == true);
    CHECK(b.camManualFar == doctest::Approx(1234.0f));
    REQUIRE(b.customPresets.size() == 1);
    CHECK(std::string(b.customPresets[0].name) == "MyPreset");
    CHECK(b.customPresets[0].r == doctest::Approx(0.21f));
    CHECK(b.customPresets[0].b == doctest::Approx(0.43f));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_CASE("AppConfig missing file yields defaults") {
    const auto missing =
        (std::filesystem::temp_directory_path() / "onyx_appconfig_does_not_exist.toml").string();
    AppConfig def = AppConfig::load(missing);
    CHECK(def.windowW == 1280);
    CHECK(def.windowH == 720);
    CHECK(def.uiScale == doctest::Approx(1.0f));
    CHECK(static_cast<int>(def.themeMode) == 0);
}
