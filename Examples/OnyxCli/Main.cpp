#include <Onyx/Cli/StockVerbs.h>
#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Cli/Verb.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Exchange/Exporters.h>
#include <Onyx/Render/CliVerbs.h>

#include <OnyxBox/Module.h>
#include <Prism/Module.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

int Run(int argc, char** argv) {
    Onyx::Modules::Workspace ws;
    auto added = ws.AddModule(std::make_unique<OnyxBox::Module>());
    if (added.ok()) added = ws.AddModule(std::make_unique<Prism::Module>());
    if (!added.ok()) {
        std::cerr << added.error().code << ": " << added.error().message << '\n';
        return 1;
    }

    Onyx::Cli::VerbTable table;
    table.Add(Onyx::Cli::StockVerbs());
    table.Add(Onyx::Render::CliVerbs());
    Onyx::Exchange::AddExporters(table);
    return Onyx::Cli::Run(ws, table, argc, argv, std::cout, std::cerr);
}

}

#if defined(_WIN32)

// The wide entry point hands over the arguments without the ANSI code page narrowing them.
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> storage;
    storage.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        const std::u8string utf8 = std::filesystem::path(argv[i]).u8string();
        storage.emplace_back(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    }
    std::vector<char*> pointers;
    for (std::string& arg : storage) pointers.push_back(arg.data());
    pointers.push_back(nullptr);
    return Run(argc, pointers.data());
}

#else

int main(int argc, char** argv) {
    return Run(argc, argv);
}

#endif
