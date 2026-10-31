#include <doctest/doctest.h>

#include <imgui.h>

#include <string>

#include "../Ui/Support/HeadlessImGui.h"
#include "Onyx/Shell/Shell.h"
#include "Onyx/Shell/StockCommands.h"
#include "Support/ShellSupport.h"

using namespace Onyx;
using Onyx::TestSupport::TempDir;
using Onyx::TestSupport::TestOptions;

namespace {

const Ui::Command& Require(Shell::Shell& shell, const char* id) {
    const Ui::Command* command = shell.Commands().Find(id);
    REQUIRE_MESSAGE(command != nullptr, id);
    return *command;
}

}

TEST_CASE("The shell registers the stock commands with their menu paths and shortcuts") {
    HeadlessImGui rig;
    TempDir dir("stockcmds");
    Shell::Shell shell(TestOptions(dir.path));

    const Ui::Command& open = Require(shell, "onyx.file.open");
    CHECK(open.menuPath == "File/Open\xE2\x80\xA6");
    REQUIRE(open.shortcut.has_value());
    CHECK(Ui::FormatShortcut(*open.shortcut) == "Ctrl+O");

    CHECK(Ui::FormatShortcut(*Require(shell, "onyx.file.closeActive").shortcut) == "Ctrl+W");
    CHECK(Ui::FormatShortcut(*Require(shell, "onyx.file.exit").shortcut) == "Alt+F4");
    CHECK(Require(shell, "onyx.file.exit").menuPath == "File/Exit");
    CHECK(Ui::FormatShortcut(*Require(shell, "onyx.view.settings").shortcut) == "Ctrl+,");
    CHECK(Require(shell, "onyx.view.settings").menuPath == "View/Settings\xE2\x80\xA6");
    CHECK(Require(shell, "onyx.view.resetLayout").menuPath == "View/Reset layout");
    Require(shell, "onyx.file.closeAll");

    for (const auto& panel : shell.Panels().All()) {
        Require(shell, ("onyx.view.toggle." + std::string(panel->Id())).c_str());
    }
}

TEST_CASE("Toggle commands flip a panel and the settings command shows the hidden panel") {
    TempDir dir("togglecmds");
    Shell::Shell shell(TestOptions(dir.path));
    Ui::UiContext ui = shell.Context();

    Ui::IPanel* log = shell.Panels().Find("onyx.log");
    REQUIRE(log != nullptr);
    CHECK(log->visible);
    REQUIRE(shell.Commands().Run("onyx.view.toggle.onyx.log", ui).ok());
    CHECK_FALSE(log->visible);
    REQUIRE(shell.Commands().Run("onyx.view.toggle.onyx.log", ui).ok());
    CHECK(log->visible);

    Ui::IPanel* settings = shell.Panels().Find("onyx.settings");
    REQUIRE(settings != nullptr);
    CHECK_FALSE(settings->visible);
    REQUIRE(shell.Commands().Run("onyx.view.settings", ui).ok());
    CHECK(settings->visible);
}

TEST_CASE("Recent commands follow the Recents list") {
    TempDir dir("recentcmds");
    Shell::Shell shell(TestOptions(dir.path));
    CHECK(shell.Commands().Find("onyx.file.openRecent.0") == nullptr);

    shell.RecentFiles().Add(dir.path / "first.obx", "obx");
    shell.RecentFiles().Add(dir.path / "second.obx", "obx");
    Shell::AddStockCommands(shell.Commands(), shell);
    const Ui::Command* newest = shell.Commands().Find("onyx.file.openRecent.0");
    REQUIRE(newest != nullptr);
    CHECK(newest->menuPath == "File/Open Recent/second.obx");
    REQUIRE(shell.Commands().Find("onyx.file.openRecent.1") != nullptr);
    CHECK(shell.Commands().Find("onyx.file.openRecent.2") == nullptr);

    shell.RecentFiles().Clear();
    Shell::AddStockCommands(shell.Commands(), shell);
    CHECK(shell.Commands().Find("onyx.file.openRecent.0") == nullptr);
}

TEST_CASE("The open command asks for a dialog that runs between frames") {
    TempDir dir("opencmd");
    Shell::Shell shell(TestOptions(dir.path));
    Ui::UiContext ui = shell.Context();
    CHECK_FALSE(shell.OpenDialogPending());
    REQUIRE(shell.Commands().Run("onyx.file.open", ui).ok());
    CHECK(shell.OpenDialogPending());
}
