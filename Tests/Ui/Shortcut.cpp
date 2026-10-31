#include <doctest/doctest.h>

#include "Support/HeadlessImGui.h"

#include <Onyx/Ui/Shortcut.h>

using namespace Onyx::Ui;

TEST_CASE("ParseShortcut reads modifiers and a key") {
    HeadlessImGui rig;
    const auto s = ParseShortcut("Ctrl+Shift+O");
    REQUIRE(s.has_value());
    CHECK(s->chord == (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_O));
    CHECK(FormatShortcut(*s) == "Ctrl+Shift+O");
}

TEST_CASE("ParseShortcut accepts the documented spellings") {
    HeadlessImGui rig;
    CHECK(ParseShortcut("Alt+F4")->chord == (ImGuiMod_Alt | ImGuiKey_F4));
    CHECK(ParseShortcut("F1")->chord == ImGuiKey_F1);
    CHECK(ParseShortcut("Ctrl+,")->chord == (ImGuiMod_Ctrl | ImGuiKey_Comma));
    CHECK(ParseShortcut("Ctrl+Plus")->chord == (ImGuiMod_Ctrl | ImGuiKey_KeypadAdd));
    CHECK(ParseShortcut("ctrl+shift+o")->chord == (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_O));
    CHECK(ParseShortcut("Super+Escape")->chord == (ImGuiMod_Super | ImGuiKey_Escape));
}

TEST_CASE("FormatShortcut round-trips") {
    HeadlessImGui rig;
    for (const char* text : {"Ctrl+Shift+O", "Alt+F4", "F1", "Ctrl+,", "Ctrl+Alt+Super+Shift+Z"}) {
        const auto s = ParseShortcut(text);
        REQUIRE_MESSAGE(s.has_value(), text);
        const auto again = ParseShortcut(FormatShortcut(*s));
        REQUIRE(again.has_value());
        CHECK(again->chord == s->chord);
    }
    CHECK(FormatShortcut(*ParseShortcut("Shift+Ctrl+O")) == "Ctrl+Shift+O");
    CHECK(FormatShortcut(*ParseShortcut("Ctrl+,")) == "Ctrl+,");
}

TEST_CASE("ParseShortcut rejects bad text") {
    HeadlessImGui rig;
    CHECK_FALSE(ParseShortcut("").has_value());
    CHECK_FALSE(ParseShortcut("Ctrl+").has_value());
    CHECK_FALSE(ParseShortcut("Ctrl").has_value());
    CHECK_FALSE(ParseShortcut("Ctrl+Nope").has_value());
    CHECK_FALSE(ParseShortcut("Ctrl+Ctrl+O").has_value());
    CHECK_FALSE(ParseShortcut("O+P").has_value());
    CHECK_FALSE(ParseShortcut("+O").has_value());
}

TEST_CASE("ShortcutPressed is false without input") {
    HeadlessImGui rig;
    rig.NewFrame();
    CHECK_FALSE(ShortcutPressed(*ParseShortcut("Ctrl+O")));
    rig.EndFrame();
}
