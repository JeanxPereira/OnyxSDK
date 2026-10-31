#include <doctest/doctest.h>

#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Cli/Verb.h>
#include <Onyx/Core/Modules/DecoderRegistry.h>
#include <Onyx/Core/Modules/GameModule.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Render/CliVerbs.h>
#include <Onyx/TestKit/Png.h>
#include <Onyx/TestKit/SceneFixtures.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

using namespace Onyx;

namespace {

// Serves each named scene as one entry of a container holding no real data.
class SceneModule : public Modules::IGameModule {
public:
    explicit SceneModule(std::map<std::string, Domain::Scene> scenes) : m_scenes(std::move(scenes)) {}

    Modules::ModuleInfo Info() const override { return {"scenes", "scenes", {}, {}}; }
    Modules::ProbeResult Probe(const Modules::ProbeInput&) const override { return {90, "scenes"}; }

    void RegisterTypes(Types::TypeRegistrar& registrar) override {
        Types::TypeSpec spec;
        spec.key = "scene";
        spec.label = "scene";
        spec.media = Domain::MediaKind::Mesh;
        m_type = registrar.Add(spec);
    }

    void RegisterDecoders(Modules::DecoderRegistry& decoders) override {
        decoders.Scene(m_type, [scenes = m_scenes](Modules::DecodeContext& ctx) {
            return std::make_unique<Domain::Scene>(scenes.at(ctx.entry.name));
        });
    }

    Modules::ParseResult ParseContainer(Modules::ContainerContext& ctx) override {
        for (const auto& [name, scene] : m_scenes) {
            Domain::AssetEntry entry;
            entry.name = name;
            entry.type = m_type;
            ctx.roots.push_back(std::move(entry));
        }
        return {true};
    }

private:
    std::map<std::string, Domain::Scene> m_scenes;
    Types::TypeId m_type{};
};

struct TempDir {
    std::filesystem::path path = std::filesystem::temp_directory_path() / "onyx_render_verb_gpu";
    TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path);
        std::ofstream(path / "container.bin", std::ios::binary) << "container";
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

struct Rig {
    Modules::Workspace ws;
    Cli::VerbTable table;

    Rig() {
        std::map<std::string, Domain::Scene> scenes;
        scenes["blend"] = TestKit::BuildBlendStack().scene;
        scenes["chain"] = TestKit::BuildAnimatedChain().scene;
        ws.AddModule(std::make_unique<SceneModule>(std::move(scenes)));
        table.Add(Render::CliVerbs());
        table.Exporters().Add("png", Domain::MediaKind::Image,
                              [](const void* payload, const std::filesystem::path& out) {
                                  const auto& texture = *static_cast<const Domain::Texture*>(payload);
                                  TestKit::Image8 image;
                                  image.width = texture.width;
                                  image.height = texture.height;
                                  image.rgba = texture.pixels;
                                  return TestKit::WritePng(out, image);
                              });
    }

    int Invoke(std::vector<std::string> args, std::string& out, std::string& err) {
        args.insert(args.begin(), "onyx-test");
        std::vector<char*> argv;
        for (std::string& a : args) argv.push_back(a.data());
        std::ostringstream o, e;
        const int rc = Cli::Run(ws, table, static_cast<int>(argv.size()), argv.data(), o, e);
        out = o.str();
        err = e.str();
        return rc;
    }
};

bool Uniform(const TestKit::Image8& image) {
    for (size_t i = 4; i < image.rgba.size(); ++i) {
        if (image.rgba[i] != image.rgba[i % 4]) return false;
    }
    return true;
}

TestKit::Image8 RenderEntry(Rig& rig, const TempDir& dir, const std::string& entry, const std::string& file,
                       const std::vector<std::string>& extra = {}) {
    std::vector<std::string> args = {"render", (dir.path / "container.bin").string(),
                                     entry,    "--out",
                                     (dir.path / file).string(), "--width",
                                     "64",     "--height",
                                     "64"};
    args.insert(args.end(), extra.begin(), extra.end());
    std::string out, err;
    const int rc = rig.Invoke(args, out, err);
    REQUIRE_MESSAGE(rc == Cli::kOk, out << err);
    auto image = TestKit::ReadPng(dir.path / file);
    REQUIRE_MESSAGE(image.ok(), image.error().message);
    return std::move(image.value());
}

}

TEST_CASE("render writes a PNG of the blend stack") {
    TempDir dir;
    Rig rig;
    const TestKit::Image8 image = RenderEntry(rig, dir, "blend", "blend.png");
    CHECK(image.width == 64);
    CHECK(image.height == 64);
    CHECK_FALSE(Uniform(image));
}

TEST_CASE("render --clip 0 --time 0.5 poses the animated chain") {
    TempDir dir;
    Rig rig;
    const TestKit::Image8 rest = RenderEntry(rig, dir, "chain", "rest.png");
    const TestKit::Image8 posed = RenderEntry(rig, dir, "chain", "posed.png", {"--clip", "0", "--time", "0.5"});
    CHECK(rest.rgba != posed.rgba);
}

TEST_CASE("render reports an unknown entry as a usage error") {
    TempDir dir;
    Rig rig;
    std::string out, err;
    const int rc = rig.Invoke(
        {"render", (dir.path / "container.bin").string(), "nope", "--out", (dir.path / "x.png").string()}, out, err);
    CHECK(rc == Cli::kUsage);
}
