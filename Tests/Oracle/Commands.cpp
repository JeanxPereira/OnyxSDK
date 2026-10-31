#include "Commands.h"

#include "Report.h"

#include <Onyx/Gpu/Device.h>
#include <Onyx/Gpu/FrameRing.h>
#include <Onyx/Gpu/Upload.h>
#include <Onyx/Render/Projection.h>
#include <Onyx/Render/RenderToImage.h>
#include <Onyx/Render/SceneRenderer.h>
#include <Onyx/Render/View.h>
#include <Onyx/TestKit/ImageCompare.h>
#include <Onyx/TestKit/Png.h>
#include <Onyx/TestKit/SceneFixtures.h>
#include <Onyx/TestKit/Tolerances.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <system_error>
#include <vector>

namespace Onyx::Oracle {

namespace {

namespace fs = std::filesystem;

struct RenderedScene {
    std::string name;
    TestKit::Image8 image;
    std::string report;
};

// One device, upload context, frame ring and renderer shared by every scene of a run.
struct Session {
    std::unique_ptr<Gpu::Device> device;
    std::unique_ptr<Gpu::UploadContext> upload;
    std::unique_ptr<Gpu::FrameRing> frames;
    std::unique_ptr<Render::SceneRenderer> renderer;
};

void PrintError(const char* what, const Error& error) {
    std::fprintf(stderr, "%s: %s: %s\n", what, error.code.c_str(), error.message.c_str());
}

// Opens a session; sets exit to the process exit code when it cannot.
std::optional<Session> OpenSession(int& exit) {
    Session s;
    exit = kExitOutside;
    auto device = Gpu::Device::Create({});
    if (!device.ok()) {
        PrintError("device", device.error());
        if (device.error().code == "gpu.noDevice") {
            exit = kExitNoDevice;
        }
        return std::nullopt;
    }
    s.device = std::move(device.value());
    auto upload = Gpu::UploadContext::Create(*s.device);
    if (!upload.ok()) {
        PrintError("upload", upload.error());
        return std::nullopt;
    }
    s.upload = std::move(upload.value());
    auto frames = Gpu::FrameRing::Create(*s.device);
    if (!frames.ok()) {
        PrintError("frames", frames.error());
        return std::nullopt;
    }
    s.frames = std::move(frames.value());
    auto renderer = Render::SceneRenderer::Create(*s.device);
    if (!renderer.ok()) {
        PrintError("renderer", renderer.error());
        return std::nullopt;
    }
    s.renderer = std::move(renderer.value());
    exit = kExitWithin;
    return s;
}

// The camera the corpus is drawn with: a gradient backdrop, no grid, no skeleton.
Render::View CorpusView(const TestKit::SceneFixture& fixture) {
    Render::View view;
    view.view = fixture.view;
    view.proj = Render::Projection::Perspective(glm::radians(fixture.fovYDegrees),
                                                static_cast<float>(fixture.width) / static_cast<float>(fixture.height),
                                                fixture.nearPlane, fixture.farPlane);
    view.mode = fixture.textured ? Render::ShadingMode::Textured : Render::ShadingMode::Solid;
    view.clear = {0.0f, 0.0f, 0.0f, 1.0f};
    view.background = Render::Gradient{{0.10f, 0.11f, 0.13f}, {0.03f, 0.03f, 0.04f}};
    return view;
}

bool RenderCorpus(Session& session, std::vector<RenderedScene>& scenes) {
    for (const TestKit::SceneFixture& fixture : TestKit::BuildCorpus()) {
        std::vector<Render::DrawInfo> draws;
        {
            // The draw list comes from a load of its own: RenderToImage's reusing overload does not
            // expose the GpuScene it loads.
            auto loaded = session.renderer->Load(fixture.scene, *session.upload);
            if (!loaded.ok()) {
                PrintError(fixture.name.c_str(), loaded.error());
                return false;
            }
            const auto info = loaded.value().Draws();
            draws.assign(info.begin(), info.end());
        }
        auto texture = Render::RenderToImage(*session.renderer, *session.upload, *session.frames, fixture.scene,
                                             CorpusView(fixture), Render::Overlays{},
                                             {fixture.width, fixture.height});
        if (!texture.ok()) {
            PrintError(fixture.name.c_str(), texture.error());
            return false;
        }
        RenderedScene scene;
        scene.name = fixture.name;
        scene.image.width = texture.value().width;
        scene.image.height = texture.value().height;
        scene.image.rgba = std::move(texture.value().pixels);
        scene.report = BuildReport(fixture.name, scene.image.width, scene.image.height, Fnv1a(scene.image.rgba), draws);
        scenes.push_back(std::move(scene));
    }
    return true;
}

bool WriteText(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

bool MakeDir(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        std::fprintf(stderr, "cannot create %s: %s\n", dir.string().c_str(), ec.message().c_str());
    }
    return !ec;
}

std::vector<std::string> CorpusNames() {
    std::vector<std::string> names;
    for (const TestKit::SceneFixture& fixture : TestKit::BuildCorpus()) {
        names.push_back(fixture.name);
    }
    return names;
}

// Refuses an output directory that is the golden one or holds PNGs the oracle did not write.
bool OutputAllowed(const fs::path& dir, const fs::path& golden) {
    if (!golden.empty() && SameDirectory(dir, golden)) {
        std::fputs("--out must not be the golden directory\n", stderr);
        return false;
    }
    const std::vector<std::string> names = CorpusNames();
    if (HoldsForeignPng(dir, names)) {
        std::fprintf(stderr,
                     "refusing to write into %s: it already holds corpus PNGs the oracle did not write "
                     "(golden images?); choose an empty output directory\n",
                     dir.string().c_str());
        return false;
    }
    return true;
}

// Renders the corpus and writes <name>.png and <name>.json into dir.
bool RenderInto(Session& session, const fs::path& dir, std::vector<RenderedScene>& scenes) {
    if (!MakeDir(dir) || !RenderCorpus(session, scenes)) {
        return false;
    }
    if (!WriteText(dir / kOutputMarker, "written by OnyxOracle\n")) {
        std::fprintf(stderr, "cannot write the output marker into %s\n", dir.string().c_str());
        return false;
    }
    for (const RenderedScene& scene : scenes) {
        auto written = TestKit::WritePng(dir / (scene.name + ".png"), scene.image);
        if (!written.ok()) {
            PrintError(scene.name.c_str(), written.error());
            return false;
        }
        if (!WriteText(dir / (scene.name + ".json"), scene.report)) {
            std::fprintf(stderr, "cannot write %s.json\n", scene.name.c_str());
            return false;
        }
    }
    return true;
}

TestKit::Image8 DiffImage(const TestKit::Image8& a, const TestKit::Image8& b) {
    TestKit::Image8 diff;
    diff.width = a.width;
    diff.height = a.height;
    diff.rgba.resize(a.rgba.size());
    for (size_t i = 0; i < a.rgba.size(); ++i) {
        const int d = std::abs(static_cast<int>(a.rgba[i]) - static_cast<int>(b.rgba[i]));
        diff.rgba[i] = static_cast<std::byte>((i % 4 == 3) ? 255 : std::min(d * 4, 255));
    }
    return diff;
}

}

int RunRender(const fs::path& out, const fs::path& golden) {
    if (!OutputAllowed(out, golden)) {
        return kExitUsage;
    }
    int exit = kExitWithin;
    auto session = OpenSession(exit);
    if (!session) {
        return exit;
    }
    std::vector<RenderedScene> scenes;
    return RenderInto(*session, out, scenes) ? kExitWithin : kExitOutside;
}

int RunParity(const fs::path& golden, const fs::path& out, const fs::path& tolerances, const std::string& profileName,
              bool writeDiff) {
    if (!OutputAllowed(out, golden)) {
        return kExitUsage;
    }

    // Every golden image is read before anything renders, so each scene reports one line.
    std::vector<TestKit::Image8> wanted;
    bool missing = false;
    for (const TestKit::SceneFixture& fixture : TestKit::BuildCorpus()) {
        auto expected = TestKit::ReadPng(golden / (fixture.name + ".png"));
        if (!expected.ok()) {
            std::printf("%s: missing golden\n", fixture.name.c_str());
            missing = true;
            continue;
        }
        wanted.push_back(std::move(expected.value()));
    }
    if (missing) {
        return kExitMissingFile;
    }

    int exit = kExitWithin;
    auto session = OpenSession(exit);
    if (!session) {
        return exit;
    }
    const bool cpu = session->device->Info().deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
    const std::string profile = !profileName.empty() ? profileName : (cpu ? "software" : "hardware");

    if (!fs::exists(tolerances)) {
        std::fprintf(stderr, "tolerances file not found: %s\n", tolerances.string().c_str());
        return kExitMissingFile;
    }
    auto tolerance = TestKit::ReadTolerances(tolerances, profile);
    if (!tolerance.ok()) {
        std::fprintf(stderr, "tolerance profile '%s' unavailable in %s: %s\n", profile.c_str(),
                     tolerances.string().c_str(), tolerance.error().message.c_str());
        return kExitMissingProfile;
    }

    std::vector<RenderedScene> scenes;
    if (!RenderInto(*session, out, scenes)) {
        return kExitOutside;
    }
    if (scenes.empty() || scenes.size() != wanted.size()) {
        std::fprintf(stderr, "rendered %zu scenes but read %zu golden images\n", scenes.size(), wanted.size());
        return kExitOutside;
    }
    std::printf("profile %s on %s\n", profile.c_str(), session->device->Info().deviceName.c_str());

    int result = kExitWithin;
    for (size_t i = 0; i < scenes.size(); ++i) {
        const RenderedScene& scene = scenes[i];
        const TestKit::Image8& want = wanted[i];
        const auto metrics = TestKit::CompareRgba(scene.image.width, scene.image.height, scene.image.rgba, want.rgba);
        const bool sizeOk = want.width == scene.image.width && want.height == scene.image.height;
        const bool within = sizeOk && TestKit::WithinTolerance(metrics, tolerance.value());
        std::printf("%s: %s %s\n", scene.name.c_str(), TestKit::FormatMetrics(metrics).c_str(),
                    within ? "within" : "OUTSIDE");
        if (!within) {
            result = kExitOutside;
        }
        if (writeDiff && sizeOk) {
            auto written = TestKit::WritePng(out / (scene.name + ".diff.png"), DiffImage(scene.image, want));
            if (!written.ok()) {
                PrintError(scene.name.c_str(), written.error());
            }
        }
    }
    return result;
}

int RunReproducible(const fs::path& out, const fs::path& golden) {
    for (const fs::path& dir : {out, out / "a", out / "b"}) {
        if (!OutputAllowed(dir, golden)) {
            return kExitUsage;
        }
    }
    int exit = kExitWithin;
    auto session = OpenSession(exit);
    if (!session) {
        return exit;
    }
    std::vector<RenderedScene> first;
    std::vector<RenderedScene> second;
    if (!RenderInto(*session, out / "a", first) || !RenderInto(*session, out / "b", second)) {
        return kExitOutside;
    }
    int result = kExitWithin;
    for (size_t i = 0; i < first.size(); ++i) {
        const bool same = first[i].image.rgba == second[i].image.rgba;
        std::printf("%s: %s\n", first[i].name.c_str(), same ? "identical" : "DIFFERS");
        if (!same) {
            result = kExitOutside;
        }
    }
    return result;
}

int RunBless(const fs::path& golden) {
    for (const TestKit::SceneFixture& fixture : TestKit::BuildCorpus()) {
        const fs::path png = golden / (fixture.name + ".png");
        if (!fs::exists(png)) {
            std::fprintf(stderr, "refusing to bless: missing %s\n", png.string().c_str());
            return kExitOutside;
        }
    }
    int exit = kExitWithin;
    auto session = OpenSession(exit);
    if (!session) {
        return exit;
    }
    std::vector<RenderedScene> scenes;
    if (!RenderCorpus(*session, scenes)) {
        return kExitOutside;
    }
    for (const RenderedScene& scene : scenes) {
        if (!WriteText(golden / (scene.name + ".json"), scene.report)) {
            std::fprintf(stderr, "cannot write %s.json\n", scene.name.c_str());
            return kExitOutside;
        }
        std::printf("blessed %s.json\n", scene.name.c_str());
    }
    return kExitWithin;
}

}
