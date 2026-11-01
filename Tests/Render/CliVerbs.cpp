#include <doctest/doctest.h>

#include <Onyx/Core/Cli/Exporters.h>
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
#include "TempDir.h"

using namespace Onyx;

namespace {

struct TempFile {
    Onyx::Test::TempDir scope{"render-verb"};
    std::filesystem::path path = scope / "verb_unit.bin";
    TempFile() {
        std::ofstream f(path, std::ios::binary);
        f << "scene-bytes";
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

namespace {

void AddPng(Cli::VerbTable& table) {
    table.Exporters().Add("png", Domain::MediaKind::Image,
                          [](const void*, const std::filesystem::path&) { return Result<void>::Ok(); });
}

Tests::StubEntry Named(std::string name, std::string type) {
    Tests::StubEntry entry;
    entry.name = std::move(name);
    entry.type = std::move(type);
    entry.size = 4;
    return entry;
}

}

TEST_CASE("render rejects a non-finite --time") {
    Modules::Workspace ws;
    Cli::VerbTable table;
    table.Add(Render::CliVerbs());
    AddPng(table);
    AddSceneModule(ws);
    TempFile file;

    for (const char* bad : {"nan", "inf", "-inf"}) {
        const Outcome r = Invoke(ws, table, {"render", file.path.string(), "mesh", "--out", "a.png", "--time", bad});
        CHECK(r.rc == Cli::kUsage);
        CHECK(r.err.starts_with("usage: render "));
    }
}

TEST_CASE("render refuses a 20000 pixel width") {
    Modules::Workspace ws;
    Cli::VerbTable table;
    table.Add(Render::CliVerbs());
    AddPng(table);
    AddSceneModule(ws);
    TempFile file;

    const Outcome wide = Invoke(ws, table, {"render", file.path.string(), "mesh", "--out", "a.png", "--width", "20000"});
    CHECK(wide.rc == Cli::kUsage);
    CHECK(wide.err.starts_with("usage: render "));
    const Outcome tall = Invoke(ws, table, {"render", file.path.string(), "mesh", "--out", "a.png", "--height", "16385"});
    CHECK(tall.rc == Cli::kUsage);
}

TEST_CASE("render reports render.noScene for an image entry") {
    Modules::Workspace ws;
    Cli::VerbTable table;
    table.Add(Render::CliVerbs());
    AddPng(table);
    Tests::StubOptions options;
    options.script = {Named("pic", "image")};
    ws.AddModule(std::make_unique<Tests::StubModule>(options));
    TempFile file;

    const Outcome r = Invoke(ws, table, {"render", file.path.string(), "pic", "--out", "a.png"});
    CHECK(r.rc == Cli::kPartial);
    CHECK(r.out.starts_with("render.noScene:"));
}

TEST_CASE("render reports cli.ambiguous without --first") {
    Modules::Workspace ws;
    Cli::VerbTable table;
    table.Add(Render::CliVerbs());
    AddPng(table);
    Tests::StubOptions options;
    options.script = {Named("mesh", "scene"), Named("mesh", "scene")};
    ws.AddModule(std::make_unique<Tests::StubModule>(options));
    TempFile file;

    const Outcome r = Invoke(ws, table, {"render", file.path.string(), "mesh", "--out", "a.png"});
    CHECK(r.rc == Cli::kUsage);
    CHECK(r.err == "cli.ambiguous: 2 entries are named 'mesh': @0, @1\n");
    CHECK(r.out.empty());
}

TEST_CASE("render reports an unknown entry on out") {
    Modules::Workspace ws;
    Cli::VerbTable table;
    table.Add(Render::CliVerbs());
    AddPng(table);
    AddSceneModule(ws);
    TempFile file;

    const Outcome r = Invoke(ws, table, {"render", file.path.string(), "nope", "--out", "a.png"});
    CHECK(r.rc == Cli::kUsage);
    CHECK(r.out == "unknown entry: nope\n");
}
