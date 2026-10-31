#include <doctest/doctest.h>

#include <Onyx/Cli/StockVerbs.h>
#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Cli/Verb.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Exchange/Exporters.h>
#include <Onyx/TestKit/Png.h>
#include <Onyx/TestKit/SceneFixtures.h>

#include <cgltf.h>

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

namespace {

// The first four bytes of `path`, or an empty string when it is shorter.
std::string Magic(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::string magic(4, '\0');
    if (!f.read(magic.data(), 4)) return {};
    return magic;
}

bool ParsesWithCgltf(const std::filesystem::path& path) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    const std::string file = path.string();
    bool ok = cgltf_parse_file(&options, file.c_str(), &data) == cgltf_result_success &&
              cgltf_load_buffers(&options, data, file.c_str()) == cgltf_result_success &&
              cgltf_validate(data) == cgltf_result_success;
    if (data) cgltf_free(data);
    return ok;
}

}

TEST_CASE("the gltf exporter writes a .gltf with a sibling .bin and GLB for any other extension") {
    Tests::TempDir dir("export_gltf");
    Cli::VerbTable table;
    Exchange::AddExporters(table);
    const Cli::Exporter* gltf = table.Exporters().Find("gltf");
    REQUIRE(gltf);
    const TestKit::SceneFixture fixture = TestKit::BuildAnimatedChain();

    const auto text = dir.path / "chain.GLTF";
    REQUIRE(gltf->write(&fixture.scene, text).ok());
    CHECK(Magic(text).starts_with("{"));
    CHECK(std::filesystem::exists(dir.path / "chain.bin"));
    CHECK(ParsesWithCgltf(text));

    const auto binary = dir.path / "chain.glb";
    REQUIRE(gltf->write(&fixture.scene, binary).ok());
    CHECK(Magic(binary) == "glTF");
    CHECK(ParsesWithCgltf(binary));
}
