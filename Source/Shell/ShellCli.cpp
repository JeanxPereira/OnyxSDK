#include <iostream>
#include <string_view>

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

}
