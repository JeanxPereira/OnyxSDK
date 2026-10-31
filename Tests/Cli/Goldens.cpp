#include <doctest/doctest.h>

#include <Onyx/Cli/StockVerbs.h>
#include <Onyx/Core/Cli/Json.h>
#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Cli/Verb.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/TestKit/Goldens.h>

#include <OnyxBox/Module.h>

#include "Support/BoxFixture.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

using namespace Onyx;

namespace {

struct TempBox {
    std::filesystem::path path;

    TempBox(const std::string& name, const std::vector<std::byte>& bytes)
        : path(std::filesystem::temp_directory_path() / ("onyx_cligold_" + name + ".obx")) {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    ~TempBox() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

std::string ReplaceAll(std::string text, const std::string& from, std::string_view to) {
    for (size_t pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size())) {
        text.replace(pos, from.size(), to);
    }
    return text;
}

// Runs the verb over a fixture and returns "rc", stdout and stderr with the temp path replaced
// by `<file>`, in the shape the golden files hold.
std::string Capture(const std::string& name, const std::vector<std::byte>& box, std::vector<std::string> args) {
    Modules::Workspace ws;
    REQUIRE(ws.AddModule(std::make_unique<OnyxBox::Module>()).ok());
    Cli::VerbTable table;
    table.Add(Cli::StockVerbs());

    const TempBox file(name, box);
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

TEST_CASE("CliGolden.ProbeBox") {
    CheckGolden(Capture("probe", Tests::SampleBox(), {"probe"}), "probe-box.txt");
}

TEST_CASE("CliGolden.ListBox") {
    CheckGolden(Capture("list", Tests::SampleBox(), {"list"}), "list-box.txt");
}

TEST_CASE("CliGolden.ListBoxJson") {
    CheckGolden(Capture("listjson", Tests::SampleBox(), {"list", "--json"}), "list-box-json.txt");
}

TEST_CASE("CliGolden.DecodeBoxImage") {
    CheckGolden(Capture("decode", Tests::SampleBox(), {"decode", "img"}), "decode-box-image.txt");
}

TEST_CASE("CliGolden.DecodeBoxAmbiguous") {
    CheckGolden(Capture("ambiguous", Tests::AmbiguousBox(), {"decode", "img"}), "decode-box-ambiguous.txt");
}
