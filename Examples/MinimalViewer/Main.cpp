// The whole application: the shell supplies the window, the panels, the viewers and the CLI verbs;
// the modules supply the format knowledge and the extension one viewer.
#include <OnyxBox/Module.h>
#include <Prism/Module.h>
#include <PrismUi/Extension.h>

#include <memory>

#include "Onyx/Core/Services/Paths.h"
#include "Onyx/Shell/Shell.h"
#ifdef ONYX_WITH_MEDIA
#include "Onyx/Media/AudioOutput.h"
#include "Onyx/Media/Viewers.h"
#endif

int main(int argc, char** argv) {
    Onyx::Shell::Shell shell({.appName = "MinimalViewer",
                              .configDir = Onyx::Services::Paths::UserConfig("MinimalViewer")});
    if (!shell.Workspace().AddModule(std::make_unique<OnyxBox::Module>()).ok() ||
        !shell.Workspace().AddModule(std::make_unique<Prism::Module>()).ok()) {
        return 1;
    }
    shell.AddExtension(std::make_unique<Prism::Ui::Extension>());
#ifdef ONYX_WITH_MEDIA
    shell.SetAudioOutput(std::make_unique<Onyx::Media::AudioOutput>());
    Onyx::Media::RegisterMediaViewer(shell.Viewers());
#endif
    return shell.Run(argc, argv);
}
