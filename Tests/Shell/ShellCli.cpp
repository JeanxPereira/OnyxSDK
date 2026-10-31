#include <doctest/doctest.h>

#include <OnyxBox/Module.h>

#include <Onyx/Core/Cli/Run.h>

#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "Onyx/Shell/Shell.h"
#include "Support/ShellSupport.h"

using namespace Onyx;
using Onyx::TestSupport::TempDir;
using Onyx::TestSupport::TestOptions;
using Onyx::TestSupport::WriteObx;

namespace {

int Invoke(Shell::Shell& shell, std::vector<std::string> args, std::ostream& out, std::ostream& err) {
    std::vector<char*> argv;
    for (std::string& arg : args) argv.push_back(arg.data());
    return Shell::RunCli(shell, static_cast<int>(argv.size()), argv.data(), out, err);
}

}

TEST_CASE("RunCli probes a file through the stock verbs") {
    TempDir dir("cli_probe");
    const auto obx = WriteObx(dir.path / "fixture.obx");
    Shell::Shell shell(TestOptions(dir.path));
    REQUIRE(shell.Workspace().AddModule(std::make_unique<OnyxBox::Module>()).ok());

    std::ostringstream out;
    std::ostringstream err;
    const int code = Invoke(shell, {"onyx", "probe", obx.string()}, out, err);
    CHECK(code == Cli::kOk);
    CHECK(out.str().find("winner: obx") != std::string::npos);
    CHECK(err.str().empty());
}

TEST_CASE("RunCli reports bad usage as shell.args with the usage code") {
    TempDir dir("cli_usage");
    Shell::Shell shell(TestOptions(dir.path));
    std::ostringstream out;
    std::ostringstream err;
    const int code = Invoke(shell, {"onyx", "nosuchverb"}, out, err);
    CHECK(code == Cli::kUsage);
    CHECK(err.str().find("shell.args") != std::string::npos);
}

TEST_CASE("Run with arguments takes the command line path") {
    TempDir dir("cli_run");
    Shell::Shell shell(TestOptions(dir.path));
    shell.Verbs().Add(Cli::Verb{"ping", "", [](Modules::Workspace&, const Cli::VerbTable&, std::span<const std::string_view>,
                                               std::ostream&, std::ostream&) { return 7; }});
    std::string a0 = "onyx";
    std::string a1 = "ping";
    std::vector<char*> argv{a0.data(), a1.data()};
    CHECK(shell.Run(2, argv.data()) == 7);
}
