#pragma once

#include "Onyx/Ui/Command.h"

namespace Onyx::Shell {

class Shell;

// Registers onyx.file.open, onyx.file.closeActive, onyx.file.closeAll, onyx.file.exit,
// onyx.view.resetLayout and onyx.view.settings once (a command already present keeps its definition,
// so a host may override one), and rebuilds the generated ones: onyx.file.openRecent.<n> from the
// Recents list and onyx.view.toggle.<panelId> from the panels. Call it again after either changes.
void AddStockCommands(Ui::CommandRegistry& commands, Shell& shell);

}
