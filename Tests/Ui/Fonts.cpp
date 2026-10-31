#include <doctest/doctest.h>

#include "Onyx/Ui/AssetPaths.h"
#include "Onyx/Ui/Fonts.h"
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
