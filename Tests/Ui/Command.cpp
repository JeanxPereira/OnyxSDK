#include <doctest/doctest.h>

#include "Support/BareContext.h"
#include "Support/HeadlessImGui.h"

#include <Onyx/Ui/Command.h>

using namespace Onyx::Ui;

namespace {

Command Counting(std::string id, int& counter) {
    Command c;
    c.id = std::move(id);
    c.label = "Label";
    c.run = [&counter](UiContext&) { ++counter; };
    return c;
}

}

TEST_CASE("CommandRegistry Add Find Run Remove") {
    BareContext bare;
    UiContext ui = bare.Make();
    CommandRegistry reg;
    int count = 0;
    reg.Add(Counting("file.open", count));
    REQUIRE(reg.Find("file.open") != nullptr);
    CHECK(reg.All().size() == 1);

    CHECK(reg.Run("file.open", ui).ok());
    CHECK(count == 1);

    reg.Remove("file.open");
    CHECK(reg.Find("file.open") == nullptr);
    CHECK(reg.All().empty());
}

TEST_CASE("Run reports ui.command.unknown") {
    BareContext bare;
    UiContext ui = bare.Make();
    CommandRegistry reg;
    const auto r = reg.Run("nope", ui);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().code == "ui.command.unknown");
}

TEST_CASE("A disabled command does not run") {
    BareContext bare;
    UiContext ui = bare.Make();
    CommandRegistry reg;
    int count = 0;
    Command c = Counting("a", count);
    bool on = false;
    c.enabled = [&on](UiContext&) { return on; };
    reg.Add(std::move(c));

    CHECK(reg.Run("a", ui).ok());
    CHECK(count == 0);
    on = true;
    CHECK(reg.Run("a", ui).ok());
    CHECK(count == 1);
}

TEST_CASE("Override replaces fields and keeps the id") {
    BareContext bare;
    UiContext ui = bare.Make();
    CommandRegistry reg;
    int first = 0;
    int second = 0;
    reg.Add(Counting("a", first));
    reg.Add(Counting("b", first));

    Command replacement = Counting("ignored", second);
    replacement.label = "New";
    REQUIRE(reg.Override("a", std::move(replacement)).ok());

    CHECK(reg.All()[0].id == "a");
    CHECK(reg.Find("a")->label == "New");
    CHECK(reg.Find("ignored") == nullptr);
    CHECK(reg.Run("a", ui).ok());
    CHECK(second == 1);
    CHECK(first == 0);

    const auto missing = reg.Override("zzz", Counting("zzz", first));
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == "ui.command.unknown");
}

TEST_CASE("Add with an existing id replaces it") {
    CommandRegistry reg;
    int count = 0;
    Command a = Counting("a", count);
    a.label = "One";
    reg.Add(std::move(a));
    Command again = Counting("a", count);
    again.label = "Two";
    reg.Add(std::move(again));
    CHECK(reg.All().size() == 1);
    CHECK(reg.Find("a")->label == "Two");
}

TEST_CASE("PollShortcuts runs the first enabled command whose chord is pressed") {
    HeadlessImGui rig;
    BareContext bare;
    UiContext ui = bare.Make();
    CommandRegistry reg;
    int disabledRuns = 0;
    int firstRuns = 0;
    int secondRuns = 0;

    Command disabled = Counting("disabled", disabledRuns);
    disabled.shortcut = ParseShortcut("Ctrl+K");
    disabled.enabled = [](UiContext&) { return false; };
    Command first = Counting("first", firstRuns);
    first.shortcut = ParseShortcut("Ctrl+K");
    Command second = Counting("second", secondRuns);
    second.shortcut = ParseShortcut("Ctrl+K");
    reg.Add(std::move(disabled));
    reg.Add(std::move(first));
    reg.Add(std::move(second));

    rig.NewFrame();
    reg.PollShortcuts(ui);
    rig.EndFrame();
    CHECK(firstRuns == 0);

    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_K, true);
    rig.NewFrame();
    reg.PollShortcuts(ui);
    rig.EndFrame();

    CHECK(disabledRuns == 0);
    CHECK(firstRuns == 1);
    CHECK(secondRuns == 0);
}
