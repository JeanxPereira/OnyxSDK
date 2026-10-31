#include <doctest/doctest.h>

#include "Onyx/Ui/AssetPaths.h"
#include "Onyx/Ui/Fonts.h"
#include "Onyx/Ui/Theme.h"

#include <Onyx/Core/Services/Log.h>
#include <Onyx/Core/Services/Settings.h>

#include <atomic>
#include <string>
#include "Support/HeadlessImGui.h"

using namespace Onyx::Ui;

namespace {

std::filesystem::path FontDir() {
    return std::filesystem::path(ONYX_SOURCE_DIR) / "third_party" / "fonts";
}

}

TEST_CASE("FindAsset locates the installed fonts") {
    CHECK_FALSE(FindAsset("fonts/SFMono-Regular.otf").empty());
    CHECK(FindAsset("fonts/NoSuchFile.ttf").empty());
}

TEST_CASE("BuildFonts yields text, mono and icons") {
    HeadlessImGui rig;
    const auto result = BuildFonts(*ImGui::GetIO().Fonts, FontDir() / "SFMono-Regular.otf", 15.0f,
                                   FontDir() / "SFSymbols.ttf");
    REQUIRE(result.ok());
    CHECK(result.value().text != nullptr);
    CHECK(result.value().mono != nullptr);
    CHECK(result.value().icons != nullptr);

    // A frame that draws text and a symbol rasterises both without asserting.
    ImGui::GetIO().FontDefault = result.value().text;
    rig.NewFrame();
    ImGui::Begin("Probe");
    ImGui::TextUnformatted("Probe \xEE\x80\x80");
    ImGui::End();
    rig.EndFrame();

    // Symbols inside the merged range are present, up to the highest code point SfSymbols.h defines.
    CHECK(result.value().text->IsGlyphInFont(0xE001));
    CHECK(result.value().text->IsGlyphInFont(0xFA19));
}

TEST_CASE("BuildFonts reports a missing file") {
    HeadlessImGui rig;
    const auto missingText = BuildFonts(*ImGui::GetIO().Fonts, FontDir() / "Nope.otf", 15.0f,
                                        FontDir() / "SFSymbols.ttf");
    REQUIRE_FALSE(missingText.ok());
    CHECK(missingText.error().code == "ui.font.missing");

    const auto missingIcons = BuildFonts(*ImGui::GetIO().Fonts, FontDir() / "SFMono-Regular.otf",
                                         15.0f, FontDir() / "Nope.ttf");
    REQUIRE_FALSE(missingIcons.ok());
    CHECK(missingIcons.error().code == "ui.font.missing");
}

TEST_CASE("BuildFonts reports a missing mono font") {
    HeadlessImGui rig;
    const auto result = BuildFonts(*ImGui::GetIO().Fonts, FontDir() / "SFMono-Regular.otf", 15.0f,
                                   FontDir() / "SFSymbols.ttf", FontDir() / "NoMono.otf");
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == "ui.font.missing");
}

TEST_CASE("BuildThemeFonts builds the theme's font at its reference size") {
    HeadlessImGui rig;
    Onyx::Services::Settings settings = Onyx::Services::Settings::Empty();
    Theme theme(settings);
    theme.Mutate([](ThemeState& state) {
        state.fontPath = (FontDir() / "SFMono-Regular.otf").string();
        state.fontSizePt = 18.0f;
    });
    theme.Commit();
    REQUIRE(theme.Applied().atlasRefPx > 0.0f);

    const auto fonts = BuildThemeFonts(theme, *ImGui::GetIO().Fonts);
    REQUIRE_MESSAGE(fonts.ok(), fonts.error().code << ": " << fonts.error().message);
    CHECK(fonts.value().text != nullptr);
    CHECK(fonts.value().mono != nullptr);
    CHECK(fonts.value().text->LegacySize == doctest::Approx(theme.Applied().atlasRefPx));
}

TEST_CASE("BuildThemeFonts falls back to the bundled font and warns when the theme's font is missing") {
    HeadlessImGui rig;
    Onyx::Services::Settings settings = Onyx::Services::Settings::Empty();
    Theme theme(settings);
    theme.Mutate([](ThemeState& state) { state.fontPath = "no/such/font.ttf"; });
    theme.Commit();

    std::atomic<int> warnings{0};
    auto& log = Onyx::Services::Log::Default();
    const uint64_t sink = log.AddSink([&](const Onyx::Services::LogEntry& entry) {
        if (entry.category == "ui.fonts" && entry.message.find("no/such/font.ttf") != std::string::npos) {
            ++warnings;
        }
    }, Onyx::Services::LogLevel::Warn);
    const auto fonts = BuildThemeFonts(theme, *ImGui::GetIO().Fonts);
    log.RemoveSink(sink);

    REQUIRE_MESSAGE(fonts.ok(), fonts.error().code << ": " << fonts.error().message);
    CHECK(fonts.value().text != nullptr);
    CHECK(warnings.load() == 1);
}

TEST_CASE("BuildThemeFonts before the first Commit leaves ImGui's default font") {
    HeadlessImGui rig;
    Onyx::Services::Settings settings = Onyx::Services::Settings::Empty();
    Theme theme(settings);
    ImFontAtlas& atlas = *ImGui::GetIO().Fonts;
    const auto fonts = BuildThemeFonts(theme, atlas);
    REQUIRE_FALSE(fonts.ok());
    CHECK(fonts.error().code == "ui.font.missing");
    CHECK(atlas.Fonts.Size == 1);
}
