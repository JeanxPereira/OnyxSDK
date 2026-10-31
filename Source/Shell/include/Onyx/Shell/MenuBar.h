#pragma once

#include <string>
#include <vector>

#include "Onyx/Shell/NativeWindow.h"
#include "Onyx/Ui/Command.h"
#include "Onyx/Ui/PanelHost.h"
#include "Onyx/Ui/UiContext.h"

namespace Onyx::Shell {

// One entry of the menu bar. A Menu holds children; a Command runs `commandId`; a PanelToggle shows
// or hides the panel `panelId`.
struct MenuNode {
    enum class Kind { Menu, Command, PanelToggle, Separator };

    Kind kind = Kind::Menu;
    std::string text;
    std::string commandId;
    std::string shortcut;
    std::string panelId;
    std::vector<MenuNode> children;
};

// Groups the commands that have a menu path. "File/Open Recent/a.obx" puts a command named `a.obx`
// into the submenu `Open Recent` of `File`. Menus and items keep registration order. The View menu
// ends with one toggle per panel, after a separator when it also holds commands; it is created when
// no command names it.
std::vector<MenuNode> MenuModel(const Ui::CommandRegistry& commands, const Ui::PanelHost& panels);

// Draws the main menu bar and runs the command that was clicked after the bar is complete. Returns
// the bar's rectangle for the borderless caption hit-test.
TitleBarRect DrawMenuBar(Ui::UiContext& ui);

}
