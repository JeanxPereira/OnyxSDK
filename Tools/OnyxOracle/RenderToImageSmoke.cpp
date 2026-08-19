#include "RenderToImageSmoke.h"

// task-7-brief.md's Step 1, verbatim: "the test itself must not include
// volk or touch any Vulkan type." This TU includes ONLY the ready floor's
// own public header (which itself forward-declares VkContext and never
// pulls in volk.h -- see RenderToImage.h's own top comment), the corpus
// scene builder, and the oracle's own PngWrite helper (also Vulkan-free --
// see PngWrite.h). No <Onyx/Rendering/*.h>, no volk.h, no VkContext/
// VkCommandBuffer/Vk* symbol anywhere below -- calling RenderToImage's
// one-shot overload is genuinely all a caller needs to do to get a real
// Vulkan-rendered image, without knowing Vulkan exists.
#include <Onyx/Rendering/RenderToImage.h>

#include "CorpusScenes.h"
#include "PngWrite.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Onyx::OracleTool {

namespace {

bool AllPixelsSameAsFirst(const std::vector<uint8_t>& rgba) {
    if (rgba.size() < 4) return true;
    const uint8_t first[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i + 0] != first[0] || rgba[i + 1] != first[1] || rgba[i + 2] != first[2] ||
            rgba[i + 3] != first[3]) {
            return false;
        }
    }
    return true;
}

bool BytesIdentical(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
}

// "no Vulkan device: " -- the plain-string convention RenderToImage.h's own
// doc comment documents, checked here as a substring rather than any
// Vulkan-typed error code, keeping this whole file Vulkan-free.
bool IsNoDeviceError(const std::string& err) {
    return err.rfind("no Vulkan device: ", 0) == 0;
}

} // namespace

int RunRenderToImageSmoke() {
    CorpusScene blend = BuildBlendStack();

    // RenderRequest's `scene` field only borrows `blend.scene` for the
    // duration of each call below -- `blend` stays alive across both,
    // satisfying that contract trivially.
    Onyx::Rendering::RenderRequest request{blend.scene, blend.width, blend.height, blend.view, blend.proj,
                                            blend.mode};

    std::vector<uint8_t> a, b;
    std::string err;

    if (!Onyx::Rendering::RenderToImage(request, a, err)) {
        if (IsNoDeviceError(err)) {
            std::fprintf(stderr, "render-to-image-smoke: skip: %s\n", err.c_str());
            return 77;
        }
        std::fprintf(stderr, "render-to-image-smoke: run 1: %s\n", err.c_str());
        return 1;
    }
    if (!Onyx::Rendering::RenderToImage(request, b, err)) {
        // A device was found for run 1 -- if run 2 reports "no device" it
        // is a real bug (a device disappearing mid-process), not SKIP.
        std::fprintf(stderr, "render-to-image-smoke: run 2: %s\n", err.c_str());
        return 1;
    }

    if (AllPixelsSameAsFirst(a)) {
        std::fprintf(stderr,
                     "render-to-image-smoke: every pixel is identical (expected the blend-stack "
                     "corpus scene's geometry to rasterize, not a flat image)\n");
        return 1;
    }
    if (!BytesIdentical(a, b)) {
        std::fprintf(stderr,
                     "render-to-image-smoke: two independent RenderToImage() calls did not "
                     "produce byte-identical output\n");
        return 1;
    }

    std::string pngErr;
    WritePng("render-to-image-smoke-blend-stack.png", blend.width, blend.height, a, pngErr);

    std::printf("render-to-image-smoke: blend-stack: %dx%d non-uniform, byte-identical across 2 "
                "RenderToImage() calls -- OK\n", blend.width, blend.height);
    return 0;
}

} // namespace Onyx::OracleTool
