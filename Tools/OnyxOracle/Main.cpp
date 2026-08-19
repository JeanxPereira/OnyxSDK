#include "HeadlessGL.h"
#include "CorpusScenes.h"
#include "RenderReport.h"

#include <Onyx/Rendering/SceneRenderer.h>
#include <Onyx/RenderVk/VkContext.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using Onyx::OracleTool::CorpusScene;
using Onyx::OracleTool::HeadlessGL;
using Onyx::Rendering::SceneRenderer;

namespace {

void PrintHelp() {
    std::fprintf(stderr,
        "onyx-oracle: headless GL reference renderer for the Onyx v1 M0 oracle corpus\n"
        "\n"
        "Usage:\n"
        "  onyx-oracle --gl-smoke\n"
        "      Creates a hidden GL context, renders one 64x64 frame, exits 0 on\n"
        "      success. Exit 77 if GL init fails (no display session).\n"
        "\n"
        "  onyx-oracle --vk-smoke\n"
        "      Boots a headless Vulkan 1.3 instance/device/VMA allocator via\n"
        "      VkContext, prints the picked device name, tears down, exits 0 on\n"
        "      success. Exit 77 if no Vulkan-capable device/driver is found.\n"
        "\n"
        "  onyx-oracle render-corpus --out DIR\n"
        "      Renders all 5 corpus scenes to DIR/<name>.png + DIR/<name>.json,\n"
        "      printing one summary line per scene. Exit 0 on success. Exit 77\n"
        "      if GL init fails (no display session) -- treat this as SKIP, not\n"
        "      FAIL, in any automated caller.\n"
        "\n"
        "  onyx-oracle verify DIR_A DIR_B\n"
        "      Byte-compares the 10 corpus files (5 PNG + 5 JSON) between two\n"
        "      render-corpus output directories and prints one verdict line per\n"
        "      file. Exit 0 if all 10 files are byte-identical, 1 if any differ,\n"
        "      2 if any file is missing from either directory, 77 if DIR_B does\n"
        "      not exist at all (treat this as SKIP, not FAIL).\n");
}

// The 5 corpus scene names in BuildCorpus() order, times the 2 extensions
// render-corpus writes per scene -- the fixed 10-file set verify compares.
const char* kSceneNames[] = {"sphere-grid", "skinned-cube", "blend-stack", "joint-chain-200",
                              "sphere-grid-textured"};
const char* kExtensions[] = {".png", ".json"};

// Reads a whole file into bytes. Returns false (leaving out empty) if the
// file cannot be opened -- callers distinguish "missing" from "differs".
bool ReadWholeFile(const fs::path& path, std::vector<char>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamsize size = f.tellg();
    if (size < 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0 && !f.read(out.data(), size)) return false;
    return true;
}

// ── render-corpus ───────────────────────────────────────────────────────

int RunRenderCorpus(const fs::path& outDir) {
    HeadlessGL gl;
    std::string err;
    if (!gl.Init(err)) {
        std::fprintf(stderr, "render-corpus: %s\n", err.c_str());
        return 77;
    }

    std::error_code ec;
    fs::create_directories(outDir, ec);

    std::vector<CorpusScene> corpus = Onyx::OracleTool::BuildCorpus();
    for (const CorpusScene& cs : corpus) {
        if (!gl.BeginFrame(cs.width, cs.height, err)) {
            std::fprintf(stderr, "render-corpus: %s: %s\n", cs.name.c_str(), err.c_str());
            return 1;
        }

        // Same gradient the empty-viewport-with-content path in Viewport3D
        // uses (top/bottom colors chosen to be neutral, not app-config
        // dependent -- the oracle has no AppConfig instance, see
        // AppConfigStub.cpp).
        SceneRenderer::RenderBackground(glm::vec3(0.10f, 0.11f, 0.13f),
                                        glm::vec3(0.03f, 0.03f, 0.04f));

        // One SceneRenderer per scene: scoping it to the loop body means its
        // destructor (which calls Clear()) runs before the next iteration
        // builds a fresh one, so no GL state or GPU resource leaks across
        // scenes.
        {
            SceneRenderer renderer;
            renderer.Build(cs.scene);
            // ShadingMode::Solid is Viewport3D's default (see
            // Source/Viewers/Viewport3D.h: `shadingMode = ShadingMode::Solid`)
            // -- the oracle renders with exactly the mode a freshly opened
            // viewport would use, so this corpus is what a user actually sees.
            // Solid's shader path pins geometry/skinning/blend but never reads
            // uMetallic/normal/AO/gloss/scatter (ShaderManager.cpp gates that
            // block behind mode == Textured), so the sphere-grid-textured
            // scene overrides cs.mode to ShadingMode::Textured instead --
            // that's the variant that pins the PBR material path.
            renderer.Render(cs.view, cs.proj, cs.mode, cs.width, cs.height);

            std::vector<uint8_t> rgba;
            if (!gl.EndFrame(rgba, err)) {
                std::fprintf(stderr, "render-corpus: %s: %s\n", cs.name.c_str(), err.c_str());
                return 1;
            }

            uint64_t pixelHash = Onyx::OracleTool::Fnv1a(rgba.data(), rgba.size());

            fs::path pngPath = outDir / (cs.name + ".png");
            if (!HeadlessGL::WritePng(pngPath, cs.width, cs.height, rgba, err)) {
                std::fprintf(stderr, "render-corpus: %s: %s\n", cs.name.c_str(), err.c_str());
                return 1;
            }

            std::vector<Onyx::Rendering::RenderBatch>& batches = renderer.GetBatches();
            std::vector<size_t> paletteJointCounts;
            paletteJointCounts.reserve(batches.size());
            for (const auto& b : batches) paletteJointCounts.push_back(b.jointMap.size());

            std::string report = Onyx::OracleTool::BuildReport(
                cs.name, cs.width, cs.height, pixelHash, batches, paletteJointCounts);

            fs::path jsonPath = outDir / (cs.name + ".json");
            std::ofstream jf(jsonPath, std::ios::binary | std::ios::trunc);
            if (!jf) {
                std::fprintf(stderr, "render-corpus: %s: failed to open %s for writing\n",
                            cs.name.c_str(), jsonPath.string().c_str());
                return 1;
            }
            jf.write(report.data(), static_cast<std::streamsize>(report.size()));
            jf.close();
            if (!jf) {
                std::fprintf(stderr, "render-corpus: %s: failed writing %s\n",
                            cs.name.c_str(), jsonPath.string().c_str());
                return 1;
            }

            std::printf("%s: %dx%d pixelHash=%llu batches=%zu\n", cs.name.c_str(), cs.width,
                        cs.height, static_cast<unsigned long long>(pixelHash), batches.size());

            renderer.Clear();
        }
    }

    return 0;
}

// ── verify ───────────────────────────────────────────────────────────────

int RunVerify(const fs::path& dirA, const fs::path& dirB) {
    std::error_code ec;
    if (!fs::exists(dirB, ec) || !fs::is_directory(dirB, ec)) {
        std::fprintf(stderr, "verify: %s does not exist\n", dirB.string().c_str());
        return 77;
    }

    bool anyMissing = false;
    bool anyDiffer = false;

    for (const char* name : kSceneNames) {
        for (const char* ext : kExtensions) {
            std::string fname = std::string(name) + ext;
            fs::path pathA = dirA / fname;
            fs::path pathB = dirB / fname;

            std::vector<char> bytesA, bytesB;
            bool haveA = ReadWholeFile(pathA, bytesA);
            bool haveB = ReadWholeFile(pathB, bytesB);

            if (!haveA || !haveB) {
                anyMissing = true;
                std::printf("MISSING %s (A:%s B:%s)\n", fname.c_str(),
                            haveA ? "present" : "missing", haveB ? "present" : "missing");
                continue;
            }

            bool identical = (bytesA.size() == bytesB.size()) &&
                             (bytesA.empty() ||
                              std::memcmp(bytesA.data(), bytesB.data(), bytesA.size()) == 0);
            if (identical) {
                std::printf("OK %s\n", fname.c_str());
            } else {
                anyDiffer = true;
                std::printf("DIFFERS %s (A:%zu bytes B:%zu bytes)\n", fname.c_str(),
                            bytesA.size(), bytesB.size());
            }
        }
    }

    if (anyMissing) return 2;
    if (anyDiffer) return 1;
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--gl-smoke") == 0) {
        Onyx::OracleTool::HeadlessGL gl;
        std::string err;
        if (!gl.Init(err)) { std::fprintf(stderr, "skip: %s\n", err.c_str()); return 77; }
        if (!gl.BeginFrame(64, 64, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        std::vector<uint8_t> rgba;
        if (!gl.EndFrame(rgba, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        return rgba.size() == 64u * 64u * 4u ? 0 : 1;
    }

    if (argc >= 2 && std::strcmp(argv[1], "--vk-smoke") == 0) {
        Onyx::RenderVk::VkContext ctx;
        std::string err;
        if (!ctx.Init(/*presentSupport=*/false, err)) {
            std::fprintf(stderr, "skip: %s\n", err.c_str());
            return 77;
        }
        std::printf("device: %s\n", ctx.Info().deviceName.c_str());
        ctx.Shutdown();
        return 0;
    }

    if (argc >= 2 && std::strcmp(argv[1], "render-corpus") == 0) {
        fs::path outDir;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
                outDir = argv[++i];
            }
        }
        if (outDir.empty()) {
            std::fprintf(stderr, "render-corpus: --out DIR is required\n");
            PrintHelp();
            return 1;
        }
        return RunRenderCorpus(outDir);
    }

    if (argc >= 2 && std::strcmp(argv[1], "verify") == 0) {
        if (argc < 4) {
            std::fprintf(stderr, "verify: DIR_A and DIR_B are required\n");
            PrintHelp();
            return 1;
        }
        return RunVerify(argv[2], argv[3]);
    }

    if (argc >= 2 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0)) {
        PrintHelp();
        return 0;
    }

    std::fprintf(stderr, "onyx-oracle: no command\n\n");
    PrintHelp();
    return 1;
}
