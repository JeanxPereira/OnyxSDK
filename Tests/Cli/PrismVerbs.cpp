#include <doctest/doctest.h>

#include <Onyx/Cli/StockVerbs.h>
#include <Onyx/Core/Cli/Json.h>
#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Cli/Verb.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/TestKit/Goldens.h>

#include <Prism/Module.h>

#include "../Prism/Support/PrismFixture.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include "TempDir.h"

using namespace Onyx;

namespace {

struct TempPrism {
    Onyx::Test::TempDir scope{"cliprism"};
    std::filesystem::path path;

    explicit TempPrism(const std::string& name) : path(scope / (name + ".prm")) {
        const std::vector<std::byte> bytes = Tests::SamplePrism();
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
};

std::string ReplaceAll(std::string text, const std::string& from, std::string_view to) {
    for (size_t pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size())) {
        text.replace(pos, from.size(), to);
    }
    return text;
}

// Runs the verb over the sample and returns "exit", stdout and stderr with the temp path replaced
// by `<file>`, in the shape the golden files hold.
std::string Capture(const std::string& name, std::vector<std::string> args) {
    Modules::Workspace ws;
    REQUIRE(ws.AddModule(std::make_unique<Prism::Module>()).ok());
    Cli::VerbTable table;
    table.Add(Cli::StockVerbs());

    const TempPrism file(name);
    const std::string path = file.path.string();
    args.insert(args.begin() + 1, path);
    args.insert(args.begin(), "onyx");

    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    std::ostringstream out, err;
    const int rc = Cli::Run(ws, table, static_cast<int>(argv.size()), argv.data(), out, err);

    std::string text = "exit " + std::to_string(rc) + "\n--- stdout\n" + out.str() + "--- stderr\n" + err.str();
    text = ReplaceAll(std::move(text), Cli::JsonEscape(path), "<file>");
    return ReplaceAll(std::move(text), path, "<file>");
}

void CheckGolden(const std::string& actual, const char* golden) {
    const std::filesystem::path file = std::filesystem::path(ONYX_SOURCE_DIR) / "Tests" / "Cli" / "Goldens" / golden;
    TestKit::UpdateGolden(actual, file);
    std::string diff;
    CHECK_MESSAGE(TestKit::CompareTreeGolden(actual, file, diff), diff);
}

}

TEST_CASE("CliGolden.ProbePrism") {
    CheckGolden(Capture("probe", {"probe"}), "probe-prism.txt");
}

TEST_CASE("CliGolden.ListPrism") {
    CheckGolden(Capture("list", {"list"}), "list-prism.txt");
}

TEST_CASE("CliGolden.DecodePrismPalette") {
    CheckGolden(Capture("palette", {"decode", "palette"}), "decode-prism-palette.txt");
}

TEST_CASE("CliGolden.DecodePrismStats") {
    CheckGolden(Capture("stats", {"decode", "stats"}), "decode-prism-stats.txt");
}
