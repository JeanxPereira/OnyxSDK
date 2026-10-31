#include <iostream>
#include <string_view>
#include <system_error>

#include "Onyx/Core/Cli/Run.h"
#include "Onyx/Shell/Shell.h"

namespace Onyx::Shell {

int RunCli(Shell& shell, int argc, char** argv, std::ostream& out, std::ostream& err) {
    if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
        Cli::Run(shell.Workspace(), shell.Verbs(), 1, argv, out, out);
        return Cli::kOk;
    }
    const int code = Cli::Run(shell.Workspace(), shell.Verbs(), argc, argv, out, err);
    if (code == Cli::kUsage) {
        err << "shell.args: the command line names no known verb or misuses one\n";
    }
    return code;
}

int RunCli(Shell& shell, int argc, char** argv) {
    return RunCli(shell, argc, argv, std::cout, std::cerr);
}

ArgsRoute RouteArgs(int argc, char** argv, const Cli::VerbTable& verbs) {
    if (argc <= 1) {
        return {ArgsRoute::Kind::Gui, {}};
    }
    if (argc == 2 && !verbs.Find(argv[1])) {
        // The narrow argv is in the native code page, which is what path's char constructor reads.
        std::filesystem::path file(argv[1]);
        std::error_code ec;
        if (std::filesystem::is_regular_file(file, ec)) {
            return {ArgsRoute::Kind::GuiOpen, std::move(file)};
        }
    }
    return {ArgsRoute::Kind::Cli, {}};
}

}
