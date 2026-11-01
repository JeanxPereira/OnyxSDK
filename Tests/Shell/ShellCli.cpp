#include <doctest/doctest.h>

#include <OnyxBox/Module.h>

#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Services/Paths.h>

#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
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

TEST_CASE("RunCli prints the usage and succeeds for --help") {
    TempDir dir("cli_help");
    Shell::Shell shell(TestOptions(dir.path));
    std::ostringstream out;
    std::ostringstream err;
    CHECK(Invoke(shell, {"onyx", "--help"}, out, err) == Cli::kOk);
    CHECK(out.str().find("probe") != std::string::npos);
    CHECK(err.str().empty());
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

TEST_CASE("RouteArgs sends a lone existing file to the GUI and everything else to the CLI") {
    TempDir dir("cli_route");
    const auto obx = WriteObx(dir.path / "fixture.obx");
    Shell::Shell shell(TestOptions(dir.path));

    const auto route = [&](std::vector<std::string> args) {
        std::vector<char*> argv;
        for (std::string& arg : args) argv.push_back(arg.data());
        return Shell::RouteArgs(static_cast<int>(argv.size()), argv.data(), shell.Verbs());
    };

    CHECK(route({"onyx"}).kind == Shell::ArgsRoute::Kind::Gui);

    const Shell::ArgsRoute file = route({"onyx", obx.string()});
    CHECK(file.kind == Shell::ArgsRoute::Kind::GuiOpen);
    CHECK(std::filesystem::equivalent(file.file, obx));

    CHECK(route({"onyx", (dir.path / "missing.obx").string()}).kind == Shell::ArgsRoute::Kind::Cli);
    CHECK(route({"onyx", dir.path.string()}).kind == Shell::ArgsRoute::Kind::Cli);
    CHECK(route({"onyx", "--help"}).kind == Shell::ArgsRoute::Kind::Cli);
    CHECK(route({"onyx", "probe", obx.string()}).kind == Shell::ArgsRoute::Kind::Cli);
    CHECK(route({"onyx", obx.string(), obx.string()}).kind == Shell::ArgsRoute::Kind::Cli);

    // A verb name wins over a file of the same name.
    WriteObx(dir.path / "probe");
    const std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::current_path(dir.path);
    CHECK(route({"onyx", "probe"}).kind == Shell::ArgsRoute::Kind::Cli);
    std::filesystem::current_path(previous);
}

TEST_CASE("RouteArgs opens a non-ASCII path given in UTF-8") {
    TempDir dir("cli_route_utf8");
    const std::filesystem::path folder = dir.path / std::filesystem::path(u8"caf\u00e9-\u00fc");
    std::filesystem::create_directories(folder);
    const auto obx = WriteObx(folder / std::filesystem::path(u8"b\u00f6x.obx"));
    Shell::Shell shell(TestOptions(dir.path));

    // The bytes arrive as a host would pass them: UTF-8 narrow strings that are not the process's own command line.
    std::string program = "onyx";
    std::string file = Services::Paths::ToUtf8(obx);
    std::vector<char*> argv{program.data(), file.data()};
    const Shell::ArgsRoute route = Shell::RouteArgs(static_cast<int>(argv.size()), argv.data(), shell.Verbs());
    CHECK(route.kind == Shell::ArgsRoute::Kind::GuiOpen);
    CHECK(std::filesystem::equivalent(route.file, obx));
}

TEST_CASE("RouteArgs does not throw on a narrow path that is not UTF-8") {
    TempDir dir("cli_route_narrow");
    Shell::Shell shell(TestOptions(dir.path));

    std::string program = "onyx";
    std::string file = Services::Paths::ToUtf8(dir.path / "caf") + "\xE9.obx";
    std::vector<char*> argv{program.data(), file.data()};
    Shell::ArgsRoute route;
    CHECK_NOTHROW(route = Shell::RouteArgs(static_cast<int>(argv.size()), argv.data(), shell.Verbs()));
    CHECK(route.kind == Shell::ArgsRoute::Kind::Cli);

    // The same bytes through the narrow constructor name the file the route opens.
    std::filesystem::path narrow;
    try {
        narrow = std::filesystem::path(file);
    } catch (const std::system_error&) {
        MESSAGE("the native code page cannot hold these bytes; the open check is skipped");
        return;
    }
    WriteObx(narrow);
    CHECK_NOTHROW(route = Shell::RouteArgs(static_cast<int>(argv.size()), argv.data(), shell.Verbs()));
    CHECK(route.kind == Shell::ArgsRoute::Kind::GuiOpen);
    CHECK(route.file == narrow);
}
