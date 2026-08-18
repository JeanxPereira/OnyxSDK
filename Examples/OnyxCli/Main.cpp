// onyxbox-cli -- the generic headless CLI (Onyx::Cli::Run) wired against
// the OnyxBox example module. Proves Task 6's probe/list/extract/decode
// commands end to end without depending on any real game format.

#include <Onyx/Cli/Commands.h>
#include <Onyx/Modules/Workspace.h>
#include <Onyx/Types/TypeCatalog.h>

#include <OnyxBoxModule.h>

#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<OnyxBox::OnyxBoxModule>());
    return Onyx::Cli::Run(ws, argc, argv, std::cout, std::cerr);
}
