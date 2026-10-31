#include <doctest/doctest.h>

#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Cli/Verb.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Render/CliVerbs.h>

#include "../Cli/Support/StubModule.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

using namespace Onyx;

namespace {

struct TempFile {
    std::filesystem::path path = std::filesystem::temp_directory_path() / "onyx_render_verb_unit.bin";
    TempFile() {
        std::ofstream f(path, std::ios::binary);
        f << "scene-bytes";
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

struct Outcome {
    int rc = 0;
    std::string out;
    std::string err;
};

Outcome Invoke(Modules::Workspace& ws, const Cli::VerbTable& table, std::vector<std::string> args) {
    args.insert(args.begin(), "onyx-test");
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    std::ostringstream out, err;
    const int rc = Cli::Run(ws, table, static_cast<int>(argv.size()), argv.data(), out, err);
    return {rc, out.str(), err.str()};
}

void AddSceneModule(Modules::Workspace& ws) {
    Tests::StubOptions options;
    Tests::StubEntry entry;
    entry.name = "mesh";
    entry.type = "scene";
    entry.size = 4;
    options.script = {entry};
    ws.AddModule(std::make_unique<Tests::StubModule>(options));
}

}

TEST_CASE("render without --out exits kUsage and prints the usage") {
    Modules::Workspace ws;
    Cli::VerbTable table;
    table.Add(Render::CliVerbs());
    AddSceneModule(ws);
    TempFile file;

    const Outcome r = Invoke(ws, table, {"render", file.path.string(), "mesh"});
    CHECK(r.rc == Cli::kUsage);
    CHECK(r.err.find("usage: render <file> <entry|@path> --out <png>") != std::string::npos);

    const Outcome unknown = Invoke(ws, table, {"render", file.path.string(), "mesh", "--out", "a.png", "--bogus"});
    CHECK(unknown.rc == Cli::kUsage);
    const Outcome badNumber =
        Invoke(ws, table, {"render", file.path.string(), "mesh", "--out", "a.png", "--width", "x"});
    CHECK(badNumber.rc == Cli::kUsage);
}

TEST_CASE("render without a png exporter exits kUsage with render.noPng") {
    Modules::Workspace ws;
    Cli::VerbTable table;
    table.Add(Render::CliVerbs());
    AddSceneModule(ws);
    TempFile file;

    const Outcome r = Invoke(ws, table, {"render", file.path.string(), "mesh", "--out", "unused.png"});
    CHECK(r.rc == Cli::kUsage);
    CHECK(r.err.find("render.noPng: no png exporter registered") != std::string::npos);
}
