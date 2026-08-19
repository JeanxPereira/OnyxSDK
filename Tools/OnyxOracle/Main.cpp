#include "PngWrite.h"
#include "PngRead.h"
#include "ImageCompare.h"
#include "CorpusScenes.h"
#include "RenderReport.h"

#include <Onyx/RenderVk/OffscreenTarget.h>
#include <Onyx/RenderVk/Pipelines.h>
#include <Onyx/RenderVk/RenderContext.h>
#include <Onyx/RenderVk/SceneRendererVk.h>
#include <Onyx/RenderVk/VkContext.h>
#include <Onyx/RenderVk/VkResources.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using Onyx::OracleTool::CorpusScene;

namespace {

void PrintHelp() {
    std::fprintf(stderr,
        "onyx-oracle: headless Vulkan reference renderer for the Onyx v1 oracle corpus\n"
        "\n"
        "Usage:\n"
        "  onyx-oracle --vk-smoke\n"
        "      Boots a headless Vulkan 1.3 instance/device/VMA allocator via\n"
        "      VkContext, prints the picked device name, creates a 64x64 RGBA\n"
        "      image, uploads a checker pattern to it via a staged upload,\n"
        "      destroys it; creates a 64x64 OffscreenTarget (T4), renders and\n"
        "      reads back a full-clear frame asserting every pixel is\n"
        "      byte-exact, renders and reads back a TOP/BOTTOM-split frame\n"
        "      asserting the readback is top-down, writes both as PNGs; draws\n"
        "      ONE world-space triangle covering only the upper half of a\n"
        "      second 64x64 target through SceneRendererVk::Render() with a\n"
        "      convention projection (glm::perspective then\n"
        "      Onyx::RenderVk::VulkanProjection), asserting row 0 is covered and\n"
        "      the last row is not (T7 fix-round rider 3(b) -- the assertion\n"
        "      that would have caught the missing NDC Y-flip at T5); tears\n"
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
        "  onyx-oracle --vk-scene-smoke\n"
        "      T5: builds and renders the M0 blend-stack corpus scene through\n"
        "      SceneRendererVk (Solid mode) into a T4 OffscreenTarget, twice\n"
        "      independently, asserting (a) not every pixel equals the clear\n"
        "      color and (b) the two runs are byte-identical; repeats the same\n"
        "      two checks for a sphere-grid-textured render (Textured mode, PBR\n"
        "      material path) for extra confidence. T6 extends this to the two\n"
        "      skinned corpus scenes, same two checks each: skinned-cube (also\n"
        "      asserts its render differs from the same scene rendered with\n"
        "      every mesh part's jointMap cleared -- proof the joint palette\n"
        "      actually deforms pixels) and joint-chain-200 (200 single-joint\n"
        "      batches, pinning per-batch palette remapping at scale). Also\n"
        "      exercises RenderBackground once, asserting a non-uniform,\n"
        "      repeatable gradient whose row 0 reads topColor and last row\n"
        "      reads bottomColor (T7 fix-round rider 3(a)). T8 adds two\n"
        "      RenderContext pass checks against blend-stack: (a) a pass\n"
        "      recording vkCmdClearAttachments tints a 16x16 corner --\n"
        "      readback proves the corner differs from a no-pass render and\n"
        "      everything else is byte-identical; (b) a pass that throws\n"
        "      std::runtime_error is caught/logged/skipped (spec Sec7.1) while a\n"
        "      second registered pass still runs and zero validation messages\n"
        "      are captured. Writes PNGs for each. Exit 0 on success, 1 on any\n"
        "      assertion/GPU failure, 77 if no Vulkan-capable device/driver is\n"
        "      found.\n"
        "\n"
        "  onyx-oracle render-corpus --out DIR [--renderer vk]\n"
        "      Renders all 5 corpus scenes to DIR/<name>.png + DIR/<name>.json,\n"
        "      printing one summary line per scene, through VkContext/\n"
        "      OffscreenTarget/SceneRendererVk. --renderer accepts only \"vk\"\n"
        "      (the default; Task 11 deleted the GL renderer this flag used to\n"
        "      also select -- any other value is an error) and exists so\n"
        "      scripts naming it explicitly keep working. Exit 0 on success.\n"
        "      Exit 77 if no Vulkan-capable device/driver is found -- treat\n"
        "      this as SKIP, not FAIL, in any automated caller.\n"
        "\n"
        "  onyx-oracle verify DIR_A DIR_B\n"
        "      Byte-compares the 10 corpus files (5 PNG + 5 JSON) between two\n"
        "      render-corpus output directories and prints one verdict line per\n"
        "      file. Exit 0 if all 10 files are byte-identical, 1 if any differ,\n"
        "      2 if any file is missing from either directory, 77 if DIR_B does\n"
        "      not exist at all (treat this as SKIP, not FAIL). Used by\n"
        "      OracleReproducible to confirm two independent Vulkan\n"
        "      render-corpus runs are byte-identical (see ReproTest.cmake).\n"
        "\n"
        "  onyx-oracle compare DIR_A DIR_B [--max-channel-delta N] [--max-differing-pct P]\n"
        "                                    [--max-high-delta-pct P2] [--max-mae M] [--emit-metrics]\n"
        "      Tolerant comparison of the same corpus files verify compares (PNG\n"
        "      + JSON per scene, scene list taken from BuildCorpus() itself, not\n"
        "      a hardcoded list -- a scene BuildCorpus grows to include is never\n"
        "      silently left ungated), for comparing a Vulkan render-corpus run\n"
        "      against the frozen GL goldens (Tests/Golden/corpus, produced\n"
        "      before Task 11 deleted the GL renderer and kept as the parity\n"
        "      anchor -- or any two render-corpus output directories) where\n"
        "      pixel-exactness isn't expected. All four\n"
        "      numeric flags default to 0 (pixel-exact after PNG decode -- for\n"
        "      an actual BYTE-exact comparison of the files themselves, use\n"
        "      `verify` instead). PNGs are decoded (stb_image) and compared\n"
        "      per-pixel, per-channel; a PNG whose two files decode to different\n"
        "      dimensions always fails, regardless of the four flags below. Four\n"
        "      independent tiers must all pass (T7 fix-round's amended gate --\n"
        "      see Tools/OnyxOracle/ImageCompare.h for the full reasoning):\n"
        "        --max-channel-delta N     largest |a-b| on any channel, any\n"
        "                                  pixel (a hard-cap tripwire only --\n"
        "                                  coverage-only edge deltas are bounded\n"
        "                                  by local contrast, not semantics, so\n"
        "                                  this tier's detection power is\n"
        "                                  deliberately weak)\n"
        "        --max-differing-pct P     fraction of pixels (0-100) with ANY\n"
        "                                  nonzero delta on any channel\n"
        "        --max-high-delta-pct P2   fraction of pixels (0-100) with ANY\n"
        "                                  channel delta > 8 (isolates \"a few\n"
        "                                  pixels differ a lot\" edge noise from\n"
        "                                  \"everything differs a little\")\n"
        "        --max-mae M               per-channel mean absolute error over\n"
        "                                  the WHOLE image, max across channels\n"
        "                                  -- catches uniform whole-image drift\n"
        "                                  (gamma/lighting/mip-filter bias) the\n"
        "                                  percentage tiers structurally cannot\n"
        "      Alpha is compared like any other channel -- it is a real blend-\n"
        "      parity signal (a batch rendering opaque in one API, blended in\n"
        "      the other, shows up in alpha with identical RGB), not incidental.\n"
        "      --emit-metrics additionally prints one machine-readable line per\n"
        "      scene: `metrics <scene> maxDelta=<n> differingPct=<f>\n"
        "      highDeltaPct=<f> mae=<f>` (default off) -- the substrate a future\n"
        "      ratchet mode reads from, not itself a pass/fail signal.\n"
        "      JSONs are compared byte-exact EXCEPT the \"pixelHash\" line, which\n"
        "      is masked (skipped entirely, both sides) -- pixelHash is a hash of\n"
        "      the rendered pixel buffer itself, and two different rasterizers\n"
        "      are never expected to produce byte-identical pixels even when\n"
        "      every other reported field (batch geometry, materials, blend\n"
        "      modes, skinning) matches exactly, which is what actually pins\n"
        "      correctness at the report layer. Exit 0 if every file is within\n"
        "      tolerance, 1 if any is not, 2 if any file is missing from either\n"
        "      directory, 77 if DIR_B does not exist at all (treat this as SKIP,\n"
        "      not FAIL).\n");
}

// T7 fix round (item 5): scene names are derived from BuildCorpus() itself
// at call time, never hardcoded -- a hardcoded list silently stops
// covering a scene BuildCorpus grows to include (verify/compare would
// just never check it, no error, no warning). BuildCorpus() is pure/
// deterministic and cheap (CPU-only scene construction, no GL/Vulkan
// calls -- CorpusScenes.h's own top comment), so recomputing this on
// every call is not worth caching.
std::vector<std::string> CorpusSceneNames() {
    std::vector<std::string> names;
    for (const CorpusScene& cs : Onyx::OracleTool::BuildCorpus()) names.push_back(cs.name);
    return names;
}
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

// Vulkan's NDC Y points down where GL's points up (Pipelines.h's own
// "Camera convention" note, binding for every Vulkan draw call in this
// milestone). CorpusScene::proj is built once via a plain glm::perspective()
// call in CorpusScenes.cpp -- a file compiled OUTSIDE Onyx_RenderVk (it is
// part of the onyx-oracle executable, shared verbatim by the GL
// render-corpus path, which needs no correction and must never get one --
// see CMakeLists.txt's own account of the PUBLIC-define leak that once
// corrupted the frozen GL golden corpus this exact way), so it carries no
// Vulkan-specific correction of its own. Every Vulkan call site in this
// file that feeds a CorpusScene's projection matrix to SceneRendererVk::
// Render() must apply this correction itself first -- discovered by T7's
// pixel-level comparison against the GL goldens (every scene rendered
// upside-down without it; see task-7-report.md), which is exactly the kind
// of bug this task exists to catch. Y-only: the [-1,1] vs [0,1] clip-space
// Z range difference GLM_FORCE_DEPTH_ZERO_TO_ONE would otherwise fix is a
// SEPARATE convention this scene's near=0.1/far=100 camera setup does not
// need corrected in practice (view-space depth beyond ~0.2 units already
// maps to a positive GL-style NDC z, so nothing in this corpus's visible
// range gets Vulkan-clipped at the z=0 near-clip plane, and the resulting
// mapping stays monotonic in view depth either way, so the depth TEST
// still orders fragments correctly) -- flagged in the task report as a
// latent risk for any future scene whose geometry sits within roughly 0.2
// units of the camera, not fixed here since it is not exercised by this
// corpus and this task's mandate is parity against the frozen goldens, not
// a speculative camera-matrix rewrite.
// T7 fix round (adjudicated): the correction itself now has a name --
// Onyx::RenderVk::VulkanProjection (Include/Onyx/RenderVk/Pipelines.h,
// right next to the Camera convention note this comment used to
// duplicate) -- so every future Vulkan camera call site in this codebase
// applies the SAME helper instead of each reinventing "negate [1][1]" on
// its own (which is exactly how this file's call sites forgot it the
// first time). VkProj is now a thin alias kept only so this file's two
// call sites read the same as before.
glm::mat4 VkProj(const glm::mat4& proj) {
    return Onyx::RenderVk::VulkanProjection(proj);
}

// ── vk-scene-smoke (T5) ─────────────────────────────────────────────────

// Builds and renders `scene` through a fresh SceneRendererVk into a fresh
// w x h OffscreenTarget, TWICE, fully independently (separate Build() +
// Render() + Readback() cycles, not just two draws into the same target) --
// the stronger reproducibility claim task 5's brief asks for: not just "the
// same draw commands replay identically" but "building the scene from
// scratch a second time produces byte-identical GPU state and output."
bool RenderSceneTwice(Onyx::RenderVk::VkContext& ctx, const Onyx::RenderVk::ScenePipelines& scenePipes,
                       const Onyx::Parsers::SceneData& scene, const glm::mat4& view, const glm::mat4& proj,
                       Onyx::Rendering::ShadingMode mode, int w, int h, const float clearColor[4],
                       std::vector<uint8_t>& rgbaA, std::vector<uint8_t>& rgbaB, std::string& err) {
    auto renderOnce = [&](std::vector<uint8_t>& out) -> bool {
        Onyx::RenderVk::OffscreenTarget target;
        if (!target.Create(ctx, w, h, err)) return false;

        Onyx::RenderVk::SceneRendererVk renderer;
        if (!renderer.Build(ctx, scenePipes, scene, err)) {
            renderer.Clear(ctx);
            target.Destroy(ctx);
            return false;
        }

        bool ok = Onyx::RenderVk::Resources::OneShot(ctx, [&](VkCommandBuffer cmd) {
            target.BeginFrame(cmd, clearColor);
            renderer.Render(cmd, view, VkProj(proj), mode, w, h);
            target.EndFrame(cmd);
        }, err);
        if (!ok) {
            renderer.Clear(ctx);
            target.Destroy(ctx);
            return false;
        }

        ok = target.Readback(ctx, out, err);
        renderer.Clear(ctx);
        target.Destroy(ctx);
        return ok;
    };

    return renderOnce(rgbaA) && renderOnce(rgbaB);
}

bool AllPixelsEqual(const std::vector<uint8_t>& rgba, const uint8_t expected[4]) {
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i + 0] != expected[0] || rgba[i + 1] != expected[1] || rgba[i + 2] != expected[2] ||
            rgba[i + 3] != expected[3]) {
            return false;
        }
    }
    return true;
}

bool AllPixelsSameAsFirst(const std::vector<uint8_t>& rgba) {
    if (rgba.size() < 4) return true;
    const uint8_t first[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
    return AllPixelsEqual(rgba, first);
}

bool BytesIdentical(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
}

// T7 fix round, adjudicator-mandated rider 3(a): every pixel in `row` (0 =
// top) must be within `tolerance` of `expectedRGB` on R/G/B (alpha not
// checked -- background.frag always writes 1.0, not what this assertion
// is pinning). Pins background.frag's row-orientation flip in isolation,
// the same class of bug T7's bug #1 found in SceneRendererVk::Render()
// (see task-7-report.md) -- if background.frag's own flip regressed, the
// existing "non-uniform gradient" check would still pass (a flipped
// gradient is still non-uniform), so it alone could never have caught
// this; this can.
bool RowApproxEquals(const std::vector<uint8_t>& rgba, int width, int height, int row,
                     const uint8_t expectedRGB[3], int tolerance, std::string& detail) {
    if (row < 0 || row >= height) {
        detail = "row " + std::to_string(row) + " out of range for height " + std::to_string(height);
        return false;
    }
    const size_t rowOffset = static_cast<size_t>(row) * width * 4;
    for (int x = 0; x < width; ++x) {
        const size_t i = rowOffset + static_cast<size_t>(x) * 4;
        for (int c = 0; c < 3; ++c) {
            const int got = rgba[i + static_cast<size_t>(c)];
            const int want = expectedRGB[c];
            if (std::abs(got - want) > tolerance) {
                detail = "row " + std::to_string(row) + " x=" + std::to_string(x) + " channel " +
                         std::to_string(c) + ": got " + std::to_string(got) + " want " +
                         std::to_string(want) + " (+/-" + std::to_string(tolerance) + ")";
                return false;
            }
        }
    }
    return true;
}

// ── RenderContext pass smoke (T8) helpers ──────────────────────────────

// Records a vkCmdClearAttachments sub-rect clear, exactly the pattern
// --vk-smoke's own orientation check above uses directly on a command
// buffer -- here wrapped as what a RenderContext pass callback records,
// proving a pass can issue the same kind of raw command a hand-rolled
// caller could.
void ClearRectPass(VkCommandBuffer cmd, int x, int y, int w, int h, const float rgba[4]) {
    VkClearAttachment clearAttach{};
    clearAttach.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clearAttach.colorAttachment = 0;
    clearAttach.clearValue.color.float32[0] = rgba[0];
    clearAttach.clearValue.color.float32[1] = rgba[1];
    clearAttach.clearValue.color.float32[2] = rgba[2];
    clearAttach.clearValue.color.float32[3] = rgba[3];

    VkClearRect clearRect{};
    clearRect.rect.offset = {x, y};
    clearRect.rect.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    clearRect.baseArrayLayer = 0;
    clearRect.layerCount = 1;

    vkCmdClearAttachments(cmd, 1, &clearAttach, 1, &clearRect);
}

// Builds+renders `cs` through a fresh SceneRendererVk into a fresh
// OffscreenTarget, same shape as RenderSceneTwice's inner renderOnce above,
// but additionally invokes `passCtx->Execute()` (when non-null) between the
// scene draw and EndFrame -- exactly the point in the frame T9's Shell will
// call RenderContext::Execute() from (see RenderContext.h's class doc
// comment). `passCtx` is null for a plain "no pass" baseline render.
bool RenderBlendStackWithPasses(Onyx::RenderVk::VkContext& ctx, const Onyx::RenderVk::ScenePipelines& scenePipes,
                                 const CorpusScene& cs, Onyx::RenderVk::RenderContext* passCtx,
                                 std::vector<uint8_t>& out, std::string& err) {
    Onyx::RenderVk::OffscreenTarget target;
    if (!target.Create(ctx, cs.width, cs.height, err)) return false;

    Onyx::RenderVk::SceneRendererVk renderer;
    if (!renderer.Build(ctx, scenePipes, cs.scene, err)) {
        renderer.Clear(ctx);
        target.Destroy(ctx);
        return false;
    }

    const float clearColor[4] = {0.10f, 0.11f, 0.13f, 1.0f};
    bool ok = Onyx::RenderVk::Resources::OneShot(ctx, [&](VkCommandBuffer cmd) {
        target.BeginFrame(cmd, clearColor);
        renderer.Render(cmd, cs.view, VkProj(cs.proj), cs.mode, cs.width, cs.height);
        if (passCtx) {
            Onyx::RenderVk::FrameHandles handles{ctx.Device(), ctx.GraphicsQueue(), cmd,
                                                  ctx.GraphicsFamily(), ctx.Allocator()};
            passCtx->Execute(handles);
        }
        target.EndFrame(cmd);
    }, err);
    if (!ok) {
        renderer.Clear(ctx);
        target.Destroy(ctx);
        return false;
    }

    ok = target.Readback(ctx, out, err);
    renderer.Clear(ctx);
    target.Destroy(ctx);
    return ok;
}

// Asserts `withPass` differs from `base` SOMEWHERE inside the [rx,ry,rw,rh)
// rect (the pass had a visible effect) and is byte-identical to `base`
// EVERYWHERE outside it (the pass touched nothing else) -- the brief's
// "compare the two readbacks region-wise yourself" requirement. Region and
// whole-image checks are both done in this one pass over the pixels so a
// single mismatch anywhere outside the rect is reported precisely.
bool CornerTintOnlyInRect(const std::vector<uint8_t>& base, const std::vector<uint8_t>& withPass,
                          int width, int height, int rx, int ry, int rw, int rh, std::string& detail) {
    if (base.size() != withPass.size()) {
        detail = "readback size mismatch";
        return false;
    }
    bool rectDiffers = false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t i = (static_cast<size_t>(y) * width + x) * 4;
            const bool inRect = (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
            const bool differs = base[i + 0] != withPass[i + 0] || base[i + 1] != withPass[i + 1] ||
                                 base[i + 2] != withPass[i + 2] || base[i + 3] != withPass[i + 3];
            if (inRect) {
                if (differs) rectDiffers = true;
            } else if (differs) {
                detail = "pixel outside the pass's rect differs at (" + std::to_string(x) + "," +
                         std::to_string(y) + ")";
                return false;
            }
        }
    }
    if (!rectDiffers) {
        detail = "rect [" + std::to_string(rx) + "," + std::to_string(ry) + "," + std::to_string(rw) +
                 "x" + std::to_string(rh) + "] is byte-identical to the no-pass baseline -- the "
                 "pass had no visible effect";
        return false;
    }
    return true;
}

int RunVkSceneSmoke() {
    Onyx::RenderVk::VkContext ctx;
    std::string err;
    if (!ctx.Init(/*presentSupport=*/false, err)) {
        std::fprintf(stderr, "skip: %s\n", err.c_str());
        return 77;
    }

    Onyx::RenderVk::ScenePipelines scenePipes;
    if (!Onyx::RenderVk::Pipelines::CreateScene(ctx, scenePipes, err)) {
        std::fprintf(stderr, "vk-scene-smoke: %s\n", err.c_str());
        ctx.Shutdown();
        return 1;
    }
    Onyx::RenderVk::BackgroundPipeline bgPipe;
    if (!Onyx::RenderVk::Pipelines::CreateBackground(ctx, bgPipe, err)) {
        std::fprintf(stderr, "vk-scene-smoke: %s\n", err.c_str());
        Onyx::RenderVk::Pipelines::Destroy(ctx, scenePipes);
        ctx.Shutdown();
        return 1;
    }

    // Same gradient/clear neutral colors render-corpus uses (see
    // RunRenderCorpus above) -- not app-config dependent.
    const float clearColor[4] = {0.10f, 0.11f, 0.13f, 1.0f};
    const uint8_t clearBytes[4] = {
        static_cast<uint8_t>(clearColor[0] * 255.0f + 0.5f),
        static_cast<uint8_t>(clearColor[1] * 255.0f + 0.5f),
        static_cast<uint8_t>(clearColor[2] * 255.0f + 0.5f),
        static_cast<uint8_t>(clearColor[3] * 255.0f + 0.5f),
    };

    int rc = 0;

    // ── blend-stack, Solid mode -- the brief's required assertion ──────
    {
        CorpusScene blend = Onyx::OracleTool::BuildBlendStack();
        std::vector<uint8_t> a, b;
        if (!RenderSceneTwice(ctx, scenePipes, blend.scene, blend.view, blend.proj, blend.mode, blend.width,
                              blend.height, clearColor, a, b, err)) {
            std::fprintf(stderr, "vk-scene-smoke: blend-stack: %s\n", err.c_str());
            rc = 1;
        } else if (AllPixelsEqual(a, clearBytes)) {
            std::fprintf(stderr, "vk-scene-smoke: blend-stack: every pixel equals the clear color\n");
            rc = 1;
        } else if (!BytesIdentical(a, b)) {
            std::fprintf(stderr, "vk-scene-smoke: blend-stack: two independent Build()+Render() runs "
                                 "are not byte-identical\n");
            rc = 1;
        } else {
            std::string pngErr;
            Onyx::OracleTool::WritePng("vk-scene-smoke-blend-stack.png", blend.width, blend.height, a, pngErr);
            std::printf("vk-scene-smoke: blend-stack: %dx%d non-uniform, byte-identical across 2 runs -- OK\n",
                        blend.width, blend.height);
        }
    }

    // ── sphere-grid-textured, Textured mode -- extra confidence on the
    // PBR/metallic material path (see BuildCorpus()'s own comment: Solid
    // never reads uMetallic/normal/AO/gloss/scatter). ───────────────────
    if (rc == 0) {
        CorpusScene sphereTex = Onyx::OracleTool::BuildSphereGrid();
        sphereTex.name = "sphere-grid-textured";
        sphereTex.mode = Onyx::Rendering::ShadingMode::Textured;
        std::vector<uint8_t> a, b;
        if (!RenderSceneTwice(ctx, scenePipes, sphereTex.scene, sphereTex.view, sphereTex.proj, sphereTex.mode,
                              sphereTex.width, sphereTex.height, clearColor, a, b, err)) {
            std::fprintf(stderr, "vk-scene-smoke: sphere-grid-textured: %s\n", err.c_str());
            rc = 1;
        } else if (AllPixelsSameAsFirst(a)) {
            std::fprintf(stderr, "vk-scene-smoke: sphere-grid-textured: every pixel is identical "
                                 "(expected a rendered sphere grid, not a flat image)\n");
            rc = 1;
        } else if (!BytesIdentical(a, b)) {
            std::fprintf(stderr, "vk-scene-smoke: sphere-grid-textured: two independent Build()+Render() "
                                 "runs are not byte-identical\n");
            rc = 1;
        } else {
            std::string pngErr;
            Onyx::OracleTool::WritePng("vk-scene-smoke-sphere-grid-textured.png", sphereTex.width,
                                       sphereTex.height, a, pngErr);
            std::printf("vk-scene-smoke: sphere-grid-textured: %dx%d non-uniform, byte-identical across "
                        "2 runs -- OK\n", sphereTex.width, sphereTex.height);
        }
    }

    // ── skinned-cube, Solid mode -- pins the rest-pose skinning palette
    // path (T6: SceneRendererVk no longer carries its own copy of the
    // joint math -- it now shares GL's exact ComputeJointPalette/
    // BuildBatchPalette/BuildLocalTRS via Onyx::Rendering::JointPalette,
    // see Include/Onyx/Rendering/JointPalette.h). ───────────────────────
    if (rc == 0) {
        CorpusScene skinnedCube = Onyx::OracleTool::BuildSkinnedCube();
        std::vector<uint8_t> a, b;
        if (!RenderSceneTwice(ctx, scenePipes, skinnedCube.scene, skinnedCube.view, skinnedCube.proj,
                              skinnedCube.mode, skinnedCube.width, skinnedCube.height, clearColor, a, b, err)) {
            std::fprintf(stderr, "vk-scene-smoke: skinned-cube: %s\n", err.c_str());
            rc = 1;
        } else if (AllPixelsEqual(a, clearBytes)) {
            std::fprintf(stderr, "vk-scene-smoke: skinned-cube: every pixel equals the clear color\n");
            rc = 1;
        } else if (!BytesIdentical(a, b)) {
            std::fprintf(stderr, "vk-scene-smoke: skinned-cube: two independent Build()+Render() runs "
                                 "are not byte-identical\n");
            rc = 1;
        } else {
            std::string pngErr;
            Onyx::OracleTool::WritePng("vk-scene-smoke-skinned-cube.png", skinnedCube.width, skinnedCube.height, a,
                                       pngErr);
            std::printf("vk-scene-smoke: skinned-cube: %dx%d non-uniform, byte-identical across 2 runs -- OK\n",
                        skinnedCube.width, skinnedCube.height);

            // ── controller mandate (T5 review): prove the joint palette
            // actually deforms pixels in Vulkan -- the first pixel-level
            // proof of the skinned path. Rebuild the identical scene
            // (BuildSkinnedCube is pure/deterministic) with every mesh
            // part's jointMap cleared: BuildBatch's `useJoints` gate goes
            // false, scene.vert's FLAG_USE_JOINTS branch never fires, and
            // geometry falls back to its authored rest positions -- the
            // STRAIGHT box BuildRingedBox generated, not the pose the
            // +30deg joint-1/joint-2 palette bends it into. Cheapest
            // honest construction per the brief: no new corpus scene, no
            // weight-zeroing pass, just the jointMap the batch already
            // gates skinning on. ─────────────────────────────────────────
            CorpusScene noSkin = Onyx::OracleTool::BuildSkinnedCube();
            noSkin.name = "skinned-cube-no-skin";
            for (auto& part : noSkin.scene.meshParts) part.jointMap.clear();

            std::vector<uint8_t> nsA, nsB;
            if (!RenderSceneTwice(ctx, scenePipes, noSkin.scene, noSkin.view, noSkin.proj, noSkin.mode,
                                  noSkin.width, noSkin.height, clearColor, nsA, nsB, err)) {
                std::fprintf(stderr, "vk-scene-smoke: skinned-cube-no-skin: %s\n", err.c_str());
                rc = 1;
            } else if (!BytesIdentical(nsA, nsB)) {
                std::fprintf(stderr, "vk-scene-smoke: skinned-cube-no-skin: two independent Build()+Render() "
                                     "runs are not byte-identical\n");
                rc = 1;
            } else if (BytesIdentical(a, nsA)) {
                std::fprintf(stderr, "vk-scene-smoke: skinned-cube: render is byte-identical to the "
                                     "skinning-disabled variant -- the joint palette deformed no pixel\n");
                rc = 1;
            } else {
                std::string pngErr2;
                Onyx::OracleTool::WritePng("vk-scene-smoke-skinned-cube-no-skin.png", noSkin.width, noSkin.height,
                                           nsA, pngErr2);
                std::printf("vk-scene-smoke: skinned-cube: differs from the skinning-disabled render -- the "
                            "palette deforms pixels -- OK\n");
            }
        }
    }

    // ── joint-chain-200, Solid mode -- pins per-batch palette remapping at
    // scale (200 single-joint batches; see CorpusScenes.h's top comment:
    // every chain batch has paletteJointCount 1, so this does not exceed
    // any fixed palette limit). ──────────────────────────────────────────
    if (rc == 0) {
        CorpusScene chain = Onyx::OracleTool::BuildJointChain200();
        std::vector<uint8_t> a, b;
        if (!RenderSceneTwice(ctx, scenePipes, chain.scene, chain.view, chain.proj, chain.mode, chain.width,
                              chain.height, clearColor, a, b, err)) {
            std::fprintf(stderr, "vk-scene-smoke: joint-chain-200: %s\n", err.c_str());
            rc = 1;
        } else if (AllPixelsEqual(a, clearBytes)) {
            std::fprintf(stderr, "vk-scene-smoke: joint-chain-200: every pixel equals the clear color\n");
            rc = 1;
        } else if (!BytesIdentical(a, b)) {
            std::fprintf(stderr, "vk-scene-smoke: joint-chain-200: two independent Build()+Render() runs "
                                 "are not byte-identical\n");
            rc = 1;
        } else {
            std::string pngErr;
            Onyx::OracleTool::WritePng("vk-scene-smoke-joint-chain-200.png", chain.width, chain.height, a, pngErr);
            std::printf("vk-scene-smoke: joint-chain-200: %dx%d non-uniform, byte-identical across 2 runs -- "
                        "OK\n", chain.width, chain.height);
        }
    }

    // ── RenderBackground -- otherwise unexercised by the two scene checks
    // above (both use a flat OffscreenTarget clear, never the gradient). ─
    if (rc == 0) {
        constexpr int kW = 64, kH = 64;
        const float bgClear[4] = {0.0f, 0.0f, 0.0f, 1.0f};

        auto renderBg = [&](std::vector<uint8_t>& out) -> bool {
            Onyx::RenderVk::OffscreenTarget target;
            if (!target.Create(ctx, kW, kH, err)) return false;

            Onyx::RenderVk::SceneRendererVk renderer;
            bool bgOk = true;
            bool ok = Onyx::RenderVk::Resources::OneShot(ctx, [&](VkCommandBuffer cmd) {
                target.BeginFrame(cmd, bgClear);
                bgOk = renderer.RenderBackground(ctx, bgPipe, cmd, glm::vec3(1.0f, 0.0f, 0.0f),
                                                 glm::vec3(0.0f, 0.0f, 1.0f), err);
                target.EndFrame(cmd);
            }, err);
            if (!ok || !bgOk) {
                renderer.Clear(ctx);
                target.Destroy(ctx);
                return false;
            }

            ok = target.Readback(ctx, out, err);
            renderer.Clear(ctx);
            target.Destroy(ctx);
            return ok;
        };

        std::vector<uint8_t> a, b;
        if (!renderBg(a) || !renderBg(b)) {
            std::fprintf(stderr, "vk-scene-smoke: background: %s\n", err.c_str());
            rc = 1;
        } else if (AllPixelsSameAsFirst(a)) {
            std::fprintf(stderr, "vk-scene-smoke: background: every pixel is identical (expected a "
                                 "top/bottom gradient)\n");
            rc = 1;
        } else if (!BytesIdentical(a, b)) {
            std::fprintf(stderr, "vk-scene-smoke: background: two runs are not byte-identical\n");
            rc = 1;
        } else {
            // Rider 3(a): row 0 (top) must read topColor=red, the last row
            // (bottom) must read bottomColor=blue -- pins background.frag's
            // own top/bottom orientation, independent of anything
            // SceneRendererVk::Render() does (RenderBackground never
            // touches a projection matrix -- see Pipelines.h's Camera
            // convention note on why background.vert/frag needed their own,
            // separate flip fix).
            const uint8_t kExpectedTop[3] = {255, 0, 0};    // topColor = (1,0,0)
            const uint8_t kExpectedBottom[3] = {0, 0, 255}; // bottomColor = (0,0,1)
            std::string rowErr;
            if (!RowApproxEquals(a, kW, kH, 0, kExpectedTop, 2, rowErr)) {
                std::fprintf(stderr, "vk-scene-smoke: background: row 0 does not read topColor: %s\n",
                            rowErr.c_str());
                rc = 1;
            } else if (!RowApproxEquals(a, kW, kH, kH - 1, kExpectedBottom, 2, rowErr)) {
                std::fprintf(stderr,
                            "vk-scene-smoke: background: last row does not read bottomColor: %s\n",
                            rowErr.c_str());
                rc = 1;
            } else {
                std::printf("vk-scene-smoke: background: %dx%d non-uniform gradient, row0~=top "
                            "last-row~=bottom, byte-identical across 2 runs -- OK\n", kW, kH);
            }
        }
    }

    // ── RenderContext pass smoke (T8) ───────────────────────────────────
    // Proves the raw-floor RenderContext (Include/Onyx/RenderVk/
    // RenderContext.h) actually lets a caller record real Vulkan commands
    // into the frame, at the exact point in the frame the Shell's T9
    // Execute() call will sit: after the scene draw, before EndFrame/UI.
    // Both checks render the blend-stack corpus scene reused from above.
    if (rc == 0) {
        CorpusScene blend = Onyx::OracleTool::BuildBlendStack();

        std::vector<uint8_t> base;
        if (!RenderBlendStackWithPasses(ctx, scenePipes, blend, nullptr, base, err)) {
            std::fprintf(stderr, "vk-scene-smoke: pass-smoke: baseline render: %s\n", err.c_str());
            rc = 1;
        } else {
            // ── (a) a single pass tints a 16x16 top-left corner ─────────
            Onyx::RenderVk::RenderContext passCtx;
            const float magenta[4] = {1.0f, 0.0f, 1.0f, 1.0f};
            passCtx.AddPass("corner-tint", [&](const Onyx::RenderVk::FrameHandles& h) {
                ClearRectPass(h.cmd, 0, 0, 16, 16, magenta);
            });

            std::vector<uint8_t> withPass;
            if (!RenderBlendStackWithPasses(ctx, scenePipes, blend, &passCtx, withPass, err)) {
                std::fprintf(stderr, "vk-scene-smoke: pass-smoke: corner-tint render: %s\n", err.c_str());
                rc = 1;
            } else {
                std::string detail;
                if (!CornerTintOnlyInRect(base, withPass, blend.width, blend.height, 0, 0, 16, 16, detail)) {
                    std::fprintf(stderr, "vk-scene-smoke: pass-smoke: corner-tint: %s\n", detail.c_str());
                    rc = 1;
                } else {
                    std::string pngErr;
                    Onyx::OracleTool::WritePng("vk-scene-smoke-pass-corner-tint.png", blend.width,
                                               blend.height, withPass, pngErr);
                    std::printf("vk-scene-smoke: pass-smoke: corner-tint: 16x16 top-left corner "
                                "differs, rest byte-identical to the no-pass render -- OK\n");
                }
            }
        }
    }

    // ── (b) contained-throw: a pass that throws std::runtime_error is
    // caught/logged/skipped, a second registered pass still runs (proven
    // both via a flag and via its own visible corner tint), the frame
    // completes, and zero validation messages are captured. ──────────────
    if (rc == 0) {
        CorpusScene blend = Onyx::OracleTool::BuildBlendStack();

        std::vector<uint8_t> base;
        if (!RenderBlendStackWithPasses(ctx, scenePipes, blend, nullptr, base, err)) {
            std::fprintf(stderr, "vk-scene-smoke: pass-smoke: contained-throw: baseline render: %s\n",
                        err.c_str());
            rc = 1;
        } else {
            Onyx::RenderVk::RenderContext passCtx;
            bool secondRan = false;
            passCtx.AddPass("throws", [&](const Onyx::RenderVk::FrameHandles&) {
                throw std::runtime_error("T8 contained-throw smoke: deliberate pass failure");
            });
            const float cyan[4] = {0.0f, 1.0f, 1.0f, 1.0f};
            passCtx.AddPass("second-visible", [&](const Onyx::RenderVk::FrameHandles& h) {
                secondRan = true;
                ClearRectPass(h.cmd, blend.width - 16, blend.height - 16, 16, 16, cyan);
            });

            std::vector<uint8_t> withPasses;
            if (!RenderBlendStackWithPasses(ctx, scenePipes, blend, &passCtx, withPasses, err)) {
                std::fprintf(stderr, "vk-scene-smoke: pass-smoke: contained-throw: %s\n", err.c_str());
                rc = 1;
            } else if (!secondRan) {
                std::fprintf(stderr, "vk-scene-smoke: pass-smoke: contained-throw: the second pass "
                                     "never ran -- the first pass's throw was not contained\n");
                rc = 1;
            } else {
                std::string detail;
                if (!CornerTintOnlyInRect(base, withPasses, blend.width, blend.height, blend.width - 16,
                                          blend.height - 16, 16, 16, detail)) {
                    std::fprintf(stderr, "vk-scene-smoke: pass-smoke: contained-throw: %s\n",
                                detail.c_str());
                    rc = 1;
                } else if (ctx.ValidationMessageCount() != 0) {
                    std::fprintf(stderr, "vk-scene-smoke: pass-smoke: contained-throw: %u validation "
                                         "message(s); last: %s\n", ctx.ValidationMessageCount(),
                                ctx.LastValidationMessage().c_str());
                    rc = 1;
                } else {
                    std::string pngErr;
                    Onyx::OracleTool::WritePng("vk-scene-smoke-pass-contained-throw.png", blend.width,
                                               blend.height, withPasses, pngErr);
                    std::printf("vk-scene-smoke: pass-smoke: contained-throw: first pass's throw was "
                                "caught and skipped, the second pass still ran (visible corner tint), "
                                "frame completed, 0 validation messages -- OK\n");
                }
            }
        }
    }

    Onyx::RenderVk::Pipelines::Destroy(ctx, bgPipe);
    Onyx::RenderVk::Pipelines::Destroy(ctx, scenePipes);
    ctx.Shutdown();

    if (rc == 0 && ctx.ValidationMessageCount() != 0) {
        std::fprintf(stderr, "%u validation message(s); last: %s\n", ctx.ValidationMessageCount(),
                    ctx.LastValidationMessage().c_str());
        rc = 1;
    }
    return rc;
}

// ── render-corpus ───────────────────────────────────────────────────────
//
// Task 11 deleted the GL RunRenderCorpus (HeadlessGL/SceneRenderer) that
// used to live here and produced the frozen Tests/Golden/corpus goldens;
// RunRenderCorpusVk below is now the only render-corpus path. Same 5 corpus
// scenes, same fixed cameras, same DIR/<name>.png + DIR/<name>.json output
// shape (BuildReport is renderer-agnostic -- see RenderReport.h's top
// comment), through VkContext/OffscreenTarget/SceneRendererVk. This is
// what `compare`/VkOracleParity renders and checks against the frozen GL
// goldens, and what `verify`/OracleReproducible renders twice to confirm
// byte-identical output.
int RunRenderCorpusVk(const fs::path& outDir) {
    Onyx::RenderVk::VkContext ctx;
    std::string err;
    if (!ctx.Init(/*presentSupport=*/false, err)) {
        std::fprintf(stderr, "render-corpus: %s\n", err.c_str());
        return 77;
    }

    Onyx::RenderVk::ScenePipelines scenePipes;
    if (!Onyx::RenderVk::Pipelines::CreateScene(ctx, scenePipes, err)) {
        std::fprintf(stderr, "render-corpus: %s\n", err.c_str());
        ctx.Shutdown();
        return 1;
    }
    Onyx::RenderVk::BackgroundPipeline bgPipe;
    if (!Onyx::RenderVk::Pipelines::CreateBackground(ctx, bgPipe, err)) {
        std::fprintf(stderr, "render-corpus: %s\n", err.c_str());
        Onyx::RenderVk::Pipelines::Destroy(ctx, scenePipes);
        ctx.Shutdown();
        return 1;
    }

    std::error_code ec;
    fs::create_directories(outDir, ec);

    // Same clear color HeadlessGL::BeginFrame uses (black) before its own
    // RenderBackground call -- the background pipeline is depth-off/
    // blend-off and draws a fullscreen triangle covering every pixel, so
    // the clear color underneath is always fully overdrawn on both paths;
    // kept identical anyway so nothing about this frame's setup diverges
    // from the GL path for a reason that isn't the renderer itself. Same
    // top/bottom gradient colors RunRenderCorpus feeds SceneRenderer::
    // RenderBackground above (neutral, not app-config dependent).
    const float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const glm::vec3 topColor(0.10f, 0.11f, 0.13f);
    const glm::vec3 bottomColor(0.03f, 0.03f, 0.04f);

    int rc = 0;
    std::vector<CorpusScene> corpus = Onyx::OracleTool::BuildCorpus();
    for (const CorpusScene& cs : corpus) {
        Onyx::RenderVk::OffscreenTarget target;
        if (!target.Create(ctx, cs.width, cs.height, err)) {
            std::fprintf(stderr, "render-corpus: %s: %s\n", cs.name.c_str(), err.c_str());
            rc = 1;
            break;
        }

        Onyx::RenderVk::SceneRendererVk renderer;
        if (!renderer.Build(ctx, scenePipes, cs.scene, err)) {
            std::fprintf(stderr, "render-corpus: %s: %s\n", cs.name.c_str(), err.c_str());
            renderer.Clear(ctx);
            target.Destroy(ctx);
            rc = 1;
            break;
        }

        bool bgOk = true;
        bool ok = Onyx::RenderVk::Resources::OneShot(ctx, [&](VkCommandBuffer cmd) {
            target.BeginFrame(cmd, clearColor);
            bgOk = renderer.RenderBackground(ctx, bgPipe, cmd, topColor, bottomColor, err);
            renderer.Render(cmd, cs.view, VkProj(cs.proj), cs.mode, cs.width, cs.height);
            target.EndFrame(cmd);
        }, err);
        if (!ok || !bgOk) {
            std::fprintf(stderr, "render-corpus: %s: %s\n", cs.name.c_str(), err.c_str());
            renderer.Clear(ctx);
            target.Destroy(ctx);
            rc = 1;
            break;
        }

        std::vector<uint8_t> rgba;
        if (!target.Readback(ctx, rgba, err)) {
            std::fprintf(stderr, "render-corpus: %s: %s\n", cs.name.c_str(), err.c_str());
            renderer.Clear(ctx);
            target.Destroy(ctx);
            rc = 1;
            break;
        }

        uint64_t pixelHash = Onyx::OracleTool::Fnv1a(rgba.data(), rgba.size());

        fs::path pngPath = outDir / (cs.name + ".png");
        if (!Onyx::OracleTool::WritePng(pngPath, cs.width, cs.height, rgba, err)) {
            std::fprintf(stderr, "render-corpus: %s: %s\n", cs.name.c_str(), err.c_str());
            renderer.Clear(ctx);
            target.Destroy(ctx);
            rc = 1;
            break;
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
            renderer.Clear(ctx);
            target.Destroy(ctx);
            rc = 1;
            break;
        }
        jf.write(report.data(), static_cast<std::streamsize>(report.size()));
        jf.close();
        if (!jf) {
            std::fprintf(stderr, "render-corpus: %s: failed writing %s\n",
                        cs.name.c_str(), jsonPath.string().c_str());
            renderer.Clear(ctx);
            target.Destroy(ctx);
            rc = 1;
            break;
        }

        std::printf("%s: %dx%d pixelHash=%llu batches=%zu\n", cs.name.c_str(), cs.width,
                    cs.height, static_cast<unsigned long long>(pixelHash), batches.size());

        renderer.Clear(ctx);
        target.Destroy(ctx);
    }

    Onyx::RenderVk::Pipelines::Destroy(ctx, bgPipe);
    Onyx::RenderVk::Pipelines::Destroy(ctx, scenePipes);
    ctx.Shutdown();

    if (rc == 0 && ctx.ValidationMessageCount() != 0) {
        std::fprintf(stderr, "%u validation message(s); last: %s\n", ctx.ValidationMessageCount(),
                    ctx.LastValidationMessage().c_str());
        rc = 1;
    }
    return rc;
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

    for (const std::string& name : CorpusSceneNames()) {
        for (const char* ext : kExtensions) {
            std::string fname = name + ext;
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

// ── compare (T7, amended to the four-knob gate in the fix round) ──────────

// Tolerant sibling of RunVerify above: same corpus-file shape (scene list
// from CorpusSceneNames(), item 5 -- never a hardcoded array), but PNGs
// are decoded and compared per-pixel/per-channel against four independent
// tolerance tiers (Onyx::OracleTool::WithinTolerance -- see ImageCompare.h
// for the full reasoning behind each) instead of demanded byte-identical,
// and JSONs are compared with their "pixelHash" line masked (Onyx::
// OracleTool::JsonEqualMaskingPixelHash) instead of raw byte-for-byte --
// see RenderReport.h's doc comment on that function for why pixelHash
// specifically is the one field two different renderers are never
// expected to agree on.
int RunCompare(const fs::path& dirA, const fs::path& dirB, int maxChannelDelta,
                double maxDifferingPct, double maxHighDeltaPct, double maxMae, bool emitMetrics) {
    std::error_code ec;
    if (!fs::exists(dirB, ec) || !fs::is_directory(dirB, ec)) {
        std::fprintf(stderr, "compare: %s does not exist\n", dirB.string().c_str());
        return 77;
    }

    bool anyMissing = false;
    bool anyFail = false;

    for (const std::string& name : CorpusSceneNames()) {
        // -- PNG: decode + tolerant per-pixel/per-channel comparison --
        {
            std::string fname = name + ".png";
            fs::path pathA = dirA / fname;
            fs::path pathB = dirB / fname;

            int wA = 0, hA = 0, wB = 0, hB = 0;
            std::vector<uint8_t> rgbaA, rgbaB;
            std::string errA, errB;
            bool haveA = Onyx::OracleTool::ReadPng(pathA, wA, hA, rgbaA, errA);
            bool haveB = Onyx::OracleTool::ReadPng(pathB, wB, hB, rgbaB, errB);
            if (!haveA || !haveB) {
                anyMissing = true;
                std::printf("MISSING %s (A:%s B:%s)\n", fname.c_str(),
                            haveA ? "present" : errA.c_str(), haveB ? "present" : errB.c_str());
                continue;
            }
            if (wA != wB || hA != hB) {
                anyFail = true;
                std::printf("FAIL %s: dimension mismatch A=%dx%d B=%dx%d\n", fname.c_str(), wA, hA,
                            wB, hB);
                continue;
            }

            Onyx::OracleTool::ImageCompareResult result =
                Onyx::OracleTool::CompareRGBA(wA, hA, rgbaA, rgbaB);
            bool ok = Onyx::OracleTool::WithinTolerance(result, maxChannelDelta, maxDifferingPct,
                                                        maxHighDeltaPct, maxMae);
            std::printf("%s %s: maxChannelDelta=%d differingPct=%.4f%% highDeltaPct=%.4f%% mae=%.4f "
                        "(tolerance: maxChannelDelta<=%d differingPct<=%.4f%% "
                        "highDeltaPct<=%.4f%% mae<=%.4f)\n",
                        ok ? "OK" : "FAIL", fname.c_str(), result.maxChannelDelta,
                        result.differingPct, result.highDeltaPct, result.mae, maxChannelDelta,
                        maxDifferingPct, maxHighDeltaPct, maxMae);
            if (emitMetrics) {
                std::printf("metrics %s maxDelta=%d differingPct=%.4f highDeltaPct=%.4f mae=%.4f\n",
                            name.c_str(), result.maxChannelDelta, result.differingPct,
                            result.highDeltaPct, result.mae);
            }
            if (!ok) anyFail = true;
        }

        // -- JSON: byte-exact except the masked pixelHash line --
        {
            std::string fname = name + ".json";
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

            std::string strA(bytesA.begin(), bytesA.end());
            std::string strB(bytesB.begin(), bytesB.end());
            bool ok = Onyx::OracleTool::JsonEqualMaskingPixelHash(strA, strB);
            std::printf("%s %s (pixelHash line masked)\n", ok ? "OK" : "FAIL", fname.c_str());
            if (!ok) anyFail = true;
        }
    }

    if (anyMissing) return 2;
    if (anyFail) return 1;
    return 0;
}

} // namespace

int main(int argc, char** argv) {
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

        // T7 fix round, adjudicator-mandated rider 3(b): draw ONE world-
        // space triangle covering only the upper half of a 64x64 target
        // through a "convention projection" (glm::perspective, THEN
        // Onyx::RenderVk::VulkanProjection -- the exact two-step sequence
        // every real Vulkan camera call site must follow), and assert row
        // 0 is covered by the triangle while the last row is not. This is
        // the assertion that would have caught T5's missing NDC Y-flip --
        // every earlier check in this binary (non-uniform output,
        // run-to-run byte-identity) is orientation-blind by construction,
        // which is exactly why that bug survived T5 and T6 undetected
        // until T7's pixel comparison against the GL goldens (see
        // task-7-report.md's "bug #1"). Uses SceneRendererVk::Render()
        // itself (not a hand-rolled pipeline) so this test exercises the
        // SAME code path that had the bug.
        {
            Onyx::RenderVk::ScenePipelines triPipes;
            if (!Onyx::RenderVk::Pipelines::CreateScene(ctx, triPipes, err)) {
                std::fprintf(stderr, "vk-smoke: orientation-triangle: %s\n", err.c_str());
                ctx.Shutdown();
                return 1;
            }

            // One huge, single triangle: base along y=0 spanning far beyond
            // the visible frustum's width (x=+-1000), apex at y=1e6 -- at
            // any y within the visible frustum (a handful of units, per
            // fovy=90/near=0.1/far=100 at z=-5), the two slanted edges from
            // base to apex stay pinned near x=-+1000, i.e. WAY outside the
            // visible x range, so within the frustum this triangle reads
            // as a solid fill for every y >= 0 and nothing for y < 0 --
            // exactly a clean upper/lower half split from three vertices.
            Onyx::Parsers::SceneData triScene;
            triScene.flipZ = false; // keep the authored coordinates exactly as written
            Onyx::Parsers::MeshPart triPart;
            triPart.name = "orientation-triangle";
            triPart.materialId = 0;
            auto makeVert = [](glm::vec3 pos) {
                Onyx::Domain::GpuVertex v{};
                v.position = pos;
                v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                v.color = glm::vec4(1.0f);
                return v;
            };
            triPart.vertices = {
                makeVert(glm::vec3(-1000.0f, 0.0f, -5.0f)),
                makeVert(glm::vec3(1000.0f, 0.0f, -5.0f)),
                makeVert(glm::vec3(0.0f, 1.0e6f, -5.0f)),
            };
            triPart.indices = {0, 1, 2};
            triScene.meshParts.push_back(std::move(triPart));

            Onyx::Parsers::MaterialDesc triMat;
            triMat.baseColor[0] = 1.0f;
            triMat.baseColor[1] = 1.0f;
            triMat.baseColor[2] = 0.0f; // bright yellow -- unmistakable against a black clear
            triMat.baseColor[3] = 1.0f;
            triMat.blendMode = Onyx::Parsers::BlendMode::Normal;
            triScene.materials.push_back(triMat);

            constexpr int kTriW = 64, kTriH = 64;
            const float triClear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            const glm::mat4 triView(1.0f); // identity: camera at world origin, looking down -Z
            const glm::mat4 triProj = Onyx::RenderVk::VulkanProjection(
                glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f));

            Onyx::RenderVk::OffscreenTarget triTarget;
            bool triOk = triTarget.Create(ctx, kTriW, kTriH, err);
            Onyx::RenderVk::SceneRendererVk triRenderer;
            if (triOk) triOk = triRenderer.Build(ctx, triPipes, triScene, err);
            std::vector<uint8_t> triRgba;
            if (triOk) {
                triOk = Onyx::RenderVk::Resources::OneShot(ctx, [&](VkCommandBuffer cmd) {
                    triTarget.BeginFrame(cmd, triClear);
                    triRenderer.Render(cmd, triView, triProj, Onyx::Rendering::ShadingMode::Solid,
                                       kTriW, kTriH);
                    triTarget.EndFrame(cmd);
                }, err);
            }
            if (triOk) triOk = triTarget.Readback(ctx, triRgba, err);

            if (!triOk) {
                std::fprintf(stderr, "vk-smoke: orientation-triangle: %s\n", err.c_str());
                triRenderer.Clear(ctx);
                triTarget.Destroy(ctx);
                Onyx::RenderVk::Pipelines::Destroy(ctx, triPipes);
                ctx.Shutdown();
                return 1;
            }

            const uint8_t kTriColor[3] = {255, 255, 0};
            const uint8_t kTriBg[3] = {0, 0, 0};
            std::string rowErr;
            bool row0Covered = RowApproxEquals(triRgba, kTriW, kTriH, 0, kTriColor, 4, rowErr);
            std::string lastRowErr;
            bool lastRowUncovered =
                RowApproxEquals(triRgba, kTriW, kTriH, kTriH - 1, kTriBg, 4, lastRowErr);

            if (!row0Covered || !lastRowUncovered) {
                std::fprintf(stderr,
                             "vk-smoke: orientation-triangle mismatch (row0Covered=%d "
                             "lastRowUncovered=%d) -- row0: %s -- lastRow: %s -- expected the "
                             "upper-half triangle to cover row 0 and leave the last row as "
                             "background, which is exactly what a missing VulkanProjection() Y-flip "
                             "would invert\n",
                             row0Covered ? 1 : 0, lastRowUncovered ? 1 : 0, rowErr.c_str(),
                             lastRowErr.c_str());
                triRenderer.Clear(ctx);
                triTarget.Destroy(ctx);
                Onyx::RenderVk::Pipelines::Destroy(ctx, triPipes);
                ctx.Shutdown();
                return 1;
            }
            std::printf("orientation-triangle: row 0 covered, last row background -- verified\n");

            std::string triPngErr;
            Onyx::OracleTool::WritePng("vk-smoke-orientation-triangle.png", kTriW, kTriH, triRgba,
                                       triPngErr);

            triRenderer.Clear(ctx);
            triTarget.Destroy(ctx);
            Onyx::RenderVk::Pipelines::Destroy(ctx, triPipes);
        }

        ctx.Shutdown();

        if (ctx.ValidationMessageCount() != 0) {
            std::fprintf(stderr, "%u validation message(s); last: %s\n",
                        ctx.ValidationMessageCount(), ctx.LastValidationMessage().c_str());
            return 1;
        }
        return 0;
    }

    if (argc >= 2 && std::strcmp(argv[1], "--vk-scene-smoke") == 0) {
        return RunVkSceneSmoke();
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
        // Task 11 deleted the GL renderer this flag used to also select
        // ("gl" was the default until then) -- "vk" is the only accepted
        // value now and the default, kept accepted (not removed outright)
        // so scripts naming it explicitly (ReproTest.cmake, VkParityTest.cmake)
        // keep working unchanged.
        std::string renderer = "vk";
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
                outDir = argv[++i];
            } else if (std::strcmp(argv[i], "--renderer") == 0 && i + 1 < argc) {
                renderer = argv[++i];
            }
        }
        if (outDir.empty()) {
            std::fprintf(stderr, "render-corpus: --out DIR is required\n");
            PrintHelp();
            return 1;
        }
        if (renderer == "vk") {
            return RunRenderCorpusVk(outDir);
        }
        std::fprintf(stderr, "render-corpus: unknown --renderer '%s' (want vk -- Task 11 "
                     "deleted the GL renderer)\n", renderer.c_str());
        return 1;
    }

    if (argc >= 2 && std::strcmp(argv[1], "verify") == 0) {
        if (argc < 4) {
            std::fprintf(stderr, "verify: DIR_A and DIR_B are required\n");
            PrintHelp();
            return 1;
        }
        return RunVerify(argv[2], argv[3]);
    }

    if (argc >= 2 && std::strcmp(argv[1], "compare") == 0) {
        if (argc < 4) {
            std::fprintf(stderr, "compare: DIR_A and DIR_B are required\n");
            PrintHelp();
            return 1;
        }
        fs::path dirA = argv[2];
        fs::path dirB = argv[3];
        int maxChannelDelta = 0;
        double maxDifferingPct = 0.0;
        double maxHighDeltaPct = 0.0;
        double maxMae = 0.0;
        bool emitMetrics = false;
        for (int i = 4; i < argc; ++i) {
            if (std::strcmp(argv[i], "--max-channel-delta") == 0 && i + 1 < argc) {
                maxChannelDelta = std::atoi(argv[++i]);
            } else if (std::strcmp(argv[i], "--max-differing-pct") == 0 && i + 1 < argc) {
                maxDifferingPct = std::atof(argv[++i]);
            } else if (std::strcmp(argv[i], "--max-high-delta-pct") == 0 && i + 1 < argc) {
                maxHighDeltaPct = std::atof(argv[++i]);
            } else if (std::strcmp(argv[i], "--max-mae") == 0 && i + 1 < argc) {
                maxMae = std::atof(argv[++i]);
            } else if (std::strcmp(argv[i], "--emit-metrics") == 0) {
                emitMetrics = true;
            }
        }
        return RunCompare(dirA, dirB, maxChannelDelta, maxDifferingPct, maxHighDeltaPct, maxMae,
                          emitMetrics);
    }

    if (argc >= 2 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0)) {
        PrintHelp();
        return 0;
    }

    std::fprintf(stderr, "onyx-oracle: no command\n\n");
    PrintHelp();
    return 1;
}
