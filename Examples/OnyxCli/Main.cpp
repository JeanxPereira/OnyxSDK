#include <Onyx/Cli/StockVerbs.h>
#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Cli/Verb.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Exchange/Exporters.h>
#include <Onyx/Render/CliVerbs.h>

#include <OnyxBox/Module.h>
#include <Prism/Module.h>

#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    Onyx::Modules::Workspace ws;
    ws.AddModule(std::make_unique<OnyxBox::Module>());
    ws.AddModule(std::make_unique<Prism::Module>());

    Onyx::Cli::VerbTable table;
    table.Add(Onyx::Cli::StockVerbs());
    table.Add(Onyx::Render::CliVerbs());
    Onyx::Exchange::AddExporters(table);
    return Onyx::Cli::Run(ws, table, argc, argv, std::cout, std::cerr);
}
