#include <doctest/doctest.h>

#include <Onyx/Ui/Fonts.h>
#include <Onyx/Ui/Icons.h>
#include <Onyx/Ui/Panels/Gallery.h>

#include <imgui_internal.h>

#include "Support/ObxWorkspace.h"

using namespace Onyx;
using namespace Onyx::Ui;

TEST_CASE("Gallery is hidden by default and names itself") {
    Gallery gallery;
    CHECK_FALSE(gallery.visible);
    CHECK(gallery.Id() == "onyx.gallery");
    CHECK(gallery.Title() == "Gallery");
}

TEST_CASE("Gallery draws every page without asserts and emits geometry") {
    HeadlessImGui imgui;
    ObxUi fx;
    UiContext ui = fx.Make();
    Gallery gallery;
    gallery.visible = true;

    for (int withFonts = 0; withFonts < 2; ++withFonts) {
        FontSet fonts;
        if (withFonts) {
            ImFont* font = ImGui::GetIO().Fonts->Fonts[0];
            fonts = FontSet{font, font, font};
            ui.fonts = &fonts;
        }
        for (size_t page = 0; page < Gallery::kPageCount; ++page) {
            DrawFrames(imgui, 3, [&] { gallery.DrawPage(page, ui); });
            const ImGuiWindow* window = ImGui::FindWindowByName("panel");
            REQUIRE(window != nullptr);
            CHECK(window->DrawList->CmdBuffer.Size > 0);
            CHECK(window->DrawList->VtxBuffer.Size > 0);
        }
    }
}

TEST_CASE("Gallery draws its tab bar") {
    HeadlessImGui imgui;
    ObxUi fx;
    UiContext ui = fx.Make();
    Gallery gallery;
    gallery.visible = true;
    DrawFrames(imgui, gallery, ui);
}

TEST_CASE("the icon filter caches its matches") {
    Gallery gallery;
    const std::size_t total = IconTable().size();
    const std::vector<uint32_t>& matches = gallery.IconMatches("");
    CHECK(matches.size() == total);
    CHECK(gallery.IconRebuilds() == 1);

    CHECK(gallery.IconMatches("").size() == total);
    CHECK(gallery.IconRebuilds() == 1);

    const std::vector<uint32_t>& gear = gallery.IconMatches("gear");
    const std::size_t count = gear.size();
    CHECK(count > 0);
    CHECK(count < total);
    for (uint32_t i : gear) CHECK(std::string_view(IconTable()[i].name).find("GEAR") != std::string_view::npos);
    CHECK(gallery.IconRebuilds() == 2);
    CHECK(gallery.IconMatches("gear").size() == count);
    CHECK(gallery.IconMatches("gear").size() == count);
    CHECK(gallery.IconRebuilds() == 2);

    CHECK(gallery.IconMatches("no such icon name").empty());
}
