#include "HeadlessGL.h"
#include "PngWrite.h"
#include "CorpusScenes.h"
#include "RenderReport.h"

#include <Onyx/Rendering/SceneRenderer.h>
#include <Onyx/RenderVk/OffscreenTarget.h>
#include <Onyx/RenderVk/VkContext.h>
#include <Onyx/RenderVk/VkResources.h>

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
        "      VkContext, prints the picked device name, creates a 64x64 RGBA\n"
        "      image, uploads a checker pattern to it via a staged upload,\n"
        "      destroys it; creates a 64x64 OffscreenTarget (T4), renders and\n"
        "      reads back a full-clear frame asserting every pixel is\n"
        "      byte-exact, renders and reads back a TOP/BOTTOM-split frame\n"
        "      asserting the readback is top-down, writes both as PNGs, tears\n"
        "      everything down. Exits 0 on success. Exit 1 if any validation\n"
        "      message was captured (Debug builds with the validation layer\n"
        "      present) or any of the above assertions fail. Exit 77 if no\n"
        "      Vulkan-capable device/driver is found.\n"
        "\n"
        "  onyx-oracle --vk-validation-selftest\n"
        "      Debug-only: deliberately destroys an already-destroyed VkBuffer\n"
        "      (via a stale copy of its handle) to prove VkContext's\n"
        "      validation-message counter fires, then exits 0 if the counter\n"
        "      is nonzero afterward. Exit 1 if the counter stayed zero. Exit\n"
        "      77 if no Vulkan-capable device/driver is found, or the\n"
        "      validation layer isn't active (Release build or the layer is\n"
        "      unavailable) -- nothing to self-test in either case.\n"
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
            if (!Onyx::OracleTool::WritePng(pngPath, cs.width, cs.height, rgba, err)) {
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

        // T2 smoke: create a 64x64 RGBA image, upload a checker pattern to
        // it (staged upload -> UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY
        // via vkCmdPipelineBarrier2), then destroy it. Round-trip readback
        // is NOT required here -- that lands in T4's OffscreenTarget. The
        // bar is create/upload/destroy raising zero validation messages.
        constexpr uint32_t kImgW = 64, kImgH = 64;
        std::vector<uint8_t> checker(static_cast<size_t>(kImgW) * kImgH * 4);
        for (uint32_t y = 0; y < kImgH; ++y) {
            for (uint32_t x = 0; x < kImgW; ++x) {
                const uint8_t v = (((x / 8) + (y / 8)) % 2 == 0) ? 255 : 0;
                const size_t i = (static_cast<size_t>(y) * kImgW + x) * 4;
                checker[i + 0] = v;
                checker[i + 1] = v;
                checker[i + 2] = v;
                checker[i + 3] = 255;
            }
        }

        Onyx::RenderVk::Image2D img = Onyx::RenderVk::Resources::CreateImage2D(
            ctx, kImgW, kImgH, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT,
            err);
        if (img.img == VK_NULL_HANDLE) {
            std::fprintf(stderr, "%s\n", err.c_str());
            ctx.Shutdown();
            return 1;
        }

        if (!Onyx::RenderVk::Resources::UploadImage(ctx, img, checker.data(), err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            Onyx::RenderVk::Resources::Destroy(ctx, img);
            ctx.Shutdown();
            return 1;
        }

        Onyx::RenderVk::Resources::Destroy(ctx, img);

        // T4 smoke: OffscreenTarget's byte-exact readback, plus the
        // orientation assertion the task-4 brief demands be VERIFIED, not
        // assumed. Two renders against one 64x64 target:
        //   1) full-clear -- every readback pixel must equal the clear
        //      color exactly (no MSAA-resolve blending should survive a
        //      uniform clear).
        //   2) a background split into a distinct TOP color (rows
        //      0..31) vs BOTTOM color (rows 32..63) -- OffscreenTarget's
        //      BeginFrame only exposes a single full-target clear, so the
        //      top half is painted via a direct vkCmdClearAttachments
        //      sub-rect on the command buffer the caller already owns
        //      (the same thing a real draw would do between BeginFrame/
        //      EndFrame). Readback row 0 must be the TOP color for this
        //      target to be usable top-down by T7's byte comparisons
        //      against the GL goldens.
        {
            Onyx::RenderVk::OffscreenTarget target;
            constexpr int kW = 64, kH = 64;
            if (!target.Create(ctx, kW, kH, err)) {
                std::fprintf(stderr, "%s\n", err.c_str());
                ctx.Shutdown();
                return 1;
            }

            // -- 1) full-clear byte-exactness --
            const float clearColor[4] = {0.20f, 0.40f, 0.60f, 1.0f};
            bool ok = Onyx::RenderVk::Resources::OneShot(ctx, [&](VkCommandBuffer cmd) {
                target.BeginFrame(cmd, clearColor);
                target.EndFrame(cmd);
            }, err);
            if (!ok) {
                std::fprintf(stderr, "%s\n", err.c_str());
                target.Destroy(ctx);
                ctx.Shutdown();
                return 1;
            }

            std::vector<uint8_t> rgba;
            if (!target.Readback(ctx, rgba, err)) {
                std::fprintf(stderr, "%s\n", err.c_str());
                target.Destroy(ctx);
                ctx.Shutdown();
                return 1;
            }
            if (rgba.size() != static_cast<size_t>(kW) * kH * 4) {
                std::fprintf(stderr, "vk-smoke: readback size %zu != %dx%dx4\n", rgba.size(), kW,
                             kH);
                target.Destroy(ctx);
                ctx.Shutdown();
                return 1;
            }

            const uint8_t expected[4] = {
                static_cast<uint8_t>(clearColor[0] * 255.0f + 0.5f),
                static_cast<uint8_t>(clearColor[1] * 255.0f + 0.5f),
                static_cast<uint8_t>(clearColor[2] * 255.0f + 0.5f),
                static_cast<uint8_t>(clearColor[3] * 255.0f + 0.5f),
            };
            for (size_t i = 0; i < rgba.size(); i += 4) {
                if (rgba[i + 0] != expected[0] || rgba[i + 1] != expected[1] ||
                    rgba[i + 2] != expected[2] || rgba[i + 3] != expected[3]) {
                    std::fprintf(stderr,
                                 "vk-smoke: clear readback not byte-exact at pixel %zu: got "
                                 "(%u,%u,%u,%u) want (%u,%u,%u,%u)\n",
                                 i / 4, rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3],
                                 expected[0], expected[1], expected[2], expected[3]);
                    target.Destroy(ctx);
                    ctx.Shutdown();
                    return 1;
                }
            }

            std::string pngErr;
            if (!Onyx::OracleTool::WritePng("vk-smoke-clear.png", kW, kH, rgba, pngErr)) {
                std::fprintf(stderr, "%s\n", pngErr.c_str());
                target.Destroy(ctx);
                ctx.Shutdown();
                return 1;
            }

            // -- 2) orientation: TOP distinct from BOTTOM --
            const uint8_t kTopColor[4] = {255, 0, 0, 255};
            const uint8_t kBottomColor[4] = {0, 0, 255, 255};
            const float bottomClear[4] = {0.0f, 0.0f, 1.0f, 1.0f};

            ok = Onyx::RenderVk::Resources::OneShot(ctx, [&](VkCommandBuffer cmd) {
                target.BeginFrame(cmd, bottomClear);

                VkClearAttachment clearAttach{};
                clearAttach.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                clearAttach.colorAttachment = 0;
                clearAttach.clearValue.color.float32[0] = 1.0f;
                clearAttach.clearValue.color.float32[1] = 0.0f;
                clearAttach.clearValue.color.float32[2] = 0.0f;
                clearAttach.clearValue.color.float32[3] = 1.0f;

                VkClearRect clearRect{};
                clearRect.rect.offset = {0, 0};
                clearRect.rect.extent = {static_cast<uint32_t>(kW), static_cast<uint32_t>(kH / 2)};
                clearRect.baseArrayLayer = 0;
                clearRect.layerCount = 1;

                vkCmdClearAttachments(cmd, 1, &clearAttach, 1, &clearRect);

                target.EndFrame(cmd);
            }, err);
            if (!ok) {
                std::fprintf(stderr, "%s\n", err.c_str());
                target.Destroy(ctx);
                ctx.Shutdown();
                return 1;
            }

            std::vector<uint8_t> rgba2;
            if (!target.Readback(ctx, rgba2, err)) {
                std::fprintf(stderr, "%s\n", err.c_str());
                target.Destroy(ctx);
                ctx.Shutdown();
                return 1;
            }

            bool row0IsTop = true;
            for (int x = 0; x < kW; ++x) {
                const size_t i = static_cast<size_t>(x) * 4;
                if (rgba2[i + 0] != kTopColor[0] || rgba2[i + 1] != kTopColor[1] ||
                    rgba2[i + 2] != kTopColor[2] || rgba2[i + 3] != kTopColor[3]) {
                    row0IsTop = false;
                    break;
                }
            }
            const size_t lastRowOff = static_cast<size_t>(kH - 1) * kW * 4;
            bool lastRowIsBottom = true;
            for (int x = 0; x < kW; ++x) {
                const size_t i = lastRowOff + static_cast<size_t>(x) * 4;
                if (rgba2[i + 0] != kBottomColor[0] || rgba2[i + 1] != kBottomColor[1] ||
                    rgba2[i + 2] != kBottomColor[2] || rgba2[i + 3] != kBottomColor[3]) {
                    lastRowIsBottom = false;
                    break;
                }
            }
            if (!row0IsTop || !lastRowIsBottom) {
                std::fprintf(stderr,
                             "vk-smoke: readback orientation mismatch (row0IsTop=%d "
                             "lastRowIsBottom=%d) -- expected top-down (row 0 = TOP color)\n",
                             row0IsTop ? 1 : 0, lastRowIsBottom ? 1 : 0);
                target.Destroy(ctx);
                ctx.Shutdown();
                return 1;
            }
            std::printf("readback orientation: top-down (row 0 = TOP color) -- verified\n");

            if (!Onyx::OracleTool::WritePng("vk-smoke-orientation.png", kW, kH, rgba2, pngErr)) {
                std::fprintf(stderr, "%s\n", pngErr.c_str());
                target.Destroy(ctx);
                ctx.Shutdown();
                return 1;
            }

            target.Destroy(ctx);
        }

        ctx.Shutdown();

        if (ctx.ValidationMessageCount() != 0) {
            std::fprintf(stderr, "%u validation message(s); last: %s\n",
                        ctx.ValidationMessageCount(), ctx.LastValidationMessage().c_str());
            return 1;
        }
        return 0;
    }

    if (argc >= 2 && std::strcmp(argv[1], "--vk-validation-selftest") == 0) {
        // T2-review rider #2: a Debug-only, deliberate-error path proving
        // VkContext's validation-message counter actually fires, isolated
        // in its own CLI mode (never folded into --vk-smoke above) so the
        // ordinary smoke's "zero validation messages" assertion is never
        // touched by this deliberately-broken path.
        Onyx::RenderVk::VkContext ctx;
        std::string err;
        if (!ctx.Init(/*presentSupport=*/false, err)) {
            std::fprintf(stderr, "skip: %s\n", err.c_str());
            return 77;
        }
        if (!ctx.Info().validation) {
            std::fprintf(stderr,
                         "skip: validation layer not active (Release build, or "
                         "VK_LAYER_KHRONOS_validation unavailable) -- nothing to self-test\n");
            ctx.Shutdown();
            return 77;
        }

        Onyx::RenderVk::Buffer original = Onyx::RenderVk::Resources::CreateBuffer(
            ctx, 64, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, err);
        if (original.buf == VK_NULL_HANDLE) {
            std::fprintf(stderr, "%s\n", err.c_str());
            ctx.Shutdown();
            return 1;
        }

        // Reviewer-confirmed UB-free pattern for provoking a validation
        // message deterministically: copy the struct (both copies now
        // name the same live VkBuffer/VmaAllocation), destroy the
        // original the normal way (Resources::Destroy -- vkDestroyBuffer
        // + vmaFreeMemory, resets `original` to its default state), then
        // deliberately call raw vkDestroyBuffer directly on the STALE
        // copy's now-invalid VkBuffer handle.
        //
        // Deliberately NOT Resources::Destroy(ctx, staleCopy) here: that
        // would also call vmaFreeMemory a second time on the same
        // VmaAllocation, which is a real double-free inside VMA's own
        // allocator bookkeeping -- it crashes before any Vulkan call is
        // even made, nothing the validation layer gets a chance to
        // intercept (confirmed empirically: that version segfaults,
        // 0xC0000005, well before printing anything). A bare
        // vkDestroyBuffer on the stale handle touches only the Vulkan
        // object-lifetime-tracking validation layer, which detects the
        // already-destroyed handle, records the error via DebugCallback,
        // and does not forward the call to the driver -- no VMA state is
        // touched a second time, so nothing crashes.
        Onyx::RenderVk::Buffer staleCopy = original;
        Onyx::RenderVk::Resources::Destroy(ctx, original);
        vkDestroyBuffer(ctx.Device(), staleCopy.buf, nullptr);

        ctx.Shutdown();

        const uint32_t count = ctx.ValidationMessageCount();
        if (count == 0) {
            std::fprintf(stderr,
                         "vk-validation-selftest: expected >=1 validation message from the "
                         "stale-handle destroy, got 0 -- the counter did not fire\n");
            return 1;
        }
        std::printf("vk-validation-selftest: counter fired (%u message(s)); last: %s\n", count,
                    ctx.LastValidationMessage().c_str());
        // RESET expectations: this mode's whole point is a deliberate
        // error, so a nonzero count here is PASS -- the opposite polarity
        // of every other smoke path in this binary.
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
