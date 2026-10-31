#include <doctest/doctest.h>

#include <Onyx/Cli/StockVerbs.h>
#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Cli/Verb.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Exchange/Exporters.h>
#include <Onyx/TestKit/Png.h>

#include "../Cli/Support/StubModule.h"
#include "Support.h"

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Onyx;

TEST_CASE("AddExporters registers gltf, png and wav with their kinds") {
    Cli::VerbTable table;
    Exchange::AddExporters(table);

    const Cli::Exporter* gltf = table.Exporters().Find("gltf");
    const Cli::Exporter* png = table.Exporters().Find("png");
    const Cli::Exporter* wav = table.Exporters().Find("wav");
    REQUIRE(gltf);
    REQUIRE(png);
    REQUIRE(wav);
    CHECK(gltf->kind == Domain::MediaKind::Mesh);
    CHECK(png->kind == Domain::MediaKind::Image);
    CHECK(wav->kind == Domain::MediaKind::Audio);
    CHECK(table.Exporters().Find("obj") == nullptr);
}

TEST_CASE("decode --to png writes the file") {
    Tests::TempDir dir("decode_png");
    const auto container = dir.path / "container.bin";
    {
        std::ofstream f(container, std::ios::binary);
        f << "hello boxABCDEFGH";
    }

    Tests::StubOptions options;
    Tests::StubEntry image;
    image.name = "img";
    image.type = "image";
    image.offset = 9;
    image.size = 8;
    options.script = {image};

    Modules::Workspace ws;
    ws.AddModule(std::make_unique<Tests::StubModule>(options));
    Cli::VerbTable table;
    table.Add(Cli::StockVerbs());
    Exchange::AddExporters(table);

    const auto target = dir.path / "img.png";
    std::vector<std::string> args = {"onyx-test", "decode", container.string(), "img",
                                     "--to",      "png",    "--out",           target.string()};
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    std::ostringstream out, err;
    const int rc = Cli::Run(ws, table, static_cast<int>(argv.size()), argv.data(), out, err);

    CHECK(rc == Cli::kOk);
    const auto read = TestKit::ReadPng(target);
    REQUIRE(read.ok());
    CHECK(read.value().width == 2);
    CHECK(read.value().height == 3);
}
