// The whole application: the shell supplies the window, the panels, the viewers and the CLI verbs;
// the module supplies the format knowledge.
#include <OnyxBox/Module.h>

#include <memory>

#include "Onyx/Core/Services/Paths.h"
#include "Onyx/Shell/Shell.h"

int main(int argc, char** argv) {
    Onyx::Shell::Shell shell({.appName = "MinimalViewer",
                              .configDir = Onyx::Services::Paths::UserConfig("MinimalViewer")});
    auto added = shell.Workspace().AddModule(std::make_unique<OnyxBox::Module>());
    if (!added.ok()) {
        return 1;
    }
    return shell.Run(argc, argv);
}
