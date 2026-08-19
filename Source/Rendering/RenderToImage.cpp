#include <Onyx/Rendering/RenderToImage.h>

// See RenderToImage.h's top comment for the full design writeup (the
// projection-convention contract, why the background fields exist, and
// why Viewport3D deliberately stays off this API). This .cpp is compiled
// into Onyx_Render (root CMakeLists.txt's ONYX_RENDER_SOURCES), the same
// target every RenderVk header already assumes -- see VkContext.h's own
// include-order rule (volk.h, then vk_mem_alloc.h, before any other
// Vulkan-touching header), which VkContext.h itself enforces.
#include <Onyx/RenderVk/OffscreenTarget.h>
#include <Onyx/RenderVk/Pipelines.h>
#include <Onyx/RenderVk/SceneRendererVk.h>
#include <Onyx/RenderVk/VkContext.h>
#include <Onyx/RenderVk/VkResources.h>

namespace Onyx::Rendering {

namespace {

bool ValidateRequest(const RenderRequest& request, std::string& err) {
    if (request.width <= 0 || request.height <= 0) {
        err = "RenderToImage: width/height must both be > 0";
        return false;
    }
    return true;
}

} // namespace

bool RenderToImage(VkContext& ctx, const RenderRequest& request, std::vector<uint8_t>& rgbaOut,
                    std::string& err) {
    if (!ValidateRequest(request, err)) return false;

    // Fix round 1 (review finding, MEDIUM): this used to be an assert(),
    // which compiles out entirely under NDEBUG -- and this project's
    // default configure carries no CMAKE_BUILD_TYPE, i.e. Release (root
    // CMakeLists.txt), so the guard was silently absent in the common
    // case. This entry point's stated audience is "callers with zero
    // Vulkan knowledge" (RenderToImage.h's own top comment); a caller who
    // copies the raw-floor pattern and pre-flips proj themselves (exactly
    // what Source/Viewers/Viewport3D.cpp:352's VulkanProjection() call
    // looks like, out of context) must not get silent upside-down output
    // with no crash and no error string. So this is now a real runtime
    // check, enforced in EVERY build configuration -- not an assert. See
    // this header's own top comment for exactly what is enforced where.
    if (request.proj[1][1] <= 0.0f) {
        err = "RenderToImage: request.proj looks already Vulkan-converted (proj[1][1] <= 0) -- pass "
              "a plain projection straight out of glm::perspective(); RenderToImage applies "
              "Onyx::Rendering::VulkanProjection() internally, exactly once. See RenderRequest's "
              "doc comment (RenderToImage.h).";
        return false;
    }

    ScenePipelines scenePipes;
    if (!Pipelines::CreateScene(ctx, scenePipes, err)) return false;

    BackgroundPipeline bgPipe;
    bool haveBgPipe = false;
    if (request.hasBackground) {
        if (!Pipelines::CreateBackground(ctx, bgPipe, err)) {
            Pipelines::Destroy(ctx, scenePipes);
            return false;
        }
        haveBgPipe = true;
    }

    OffscreenTarget target;
    if (!target.Create(ctx, request.width, request.height, err)) {
        if (haveBgPipe) Pipelines::Destroy(ctx, bgPipe);
        Pipelines::Destroy(ctx, scenePipes);
        return false;
    }

    SceneRendererVk renderer;
    if (!renderer.Build(ctx, scenePipes, request.scene, err)) {
        renderer.Clear(ctx);
        target.Destroy(ctx);
        if (haveBgPipe) Pipelines::Destroy(ctx, bgPipe);
        Pipelines::Destroy(ctx, scenePipes);
        return false;
    }

    const glm::mat4 vkProj = VulkanProjection(request.proj);
    const float clear[4] = {request.clearColor.r, request.clearColor.g, request.clearColor.b,
                             request.clearColor.a};

    bool bgOk = true;
    bool ok = Resources::OneShot(ctx, [&](VkCommandBuffer cmd) {
        target.BeginFrame(cmd, clear);
        if (request.hasBackground) {
            bgOk = renderer.RenderBackground(ctx, bgPipe, cmd, request.backgroundTop, request.backgroundBottom,
                                              err);
        }
        renderer.Render(cmd, request.view, vkProj, request.mode, request.width, request.height);
        target.EndFrame(cmd);
    }, err);
    if (ok && !bgOk) ok = false; // err already set by the failing RenderBackground call above

    if (ok) ok = target.Readback(ctx, rgbaOut, err);

    renderer.Clear(ctx);
    target.Destroy(ctx);
    if (haveBgPipe) Pipelines::Destroy(ctx, bgPipe);
    Pipelines::Destroy(ctx, scenePipes);

    return ok;
}

bool RenderToImage(const RenderRequest& request, std::vector<uint8_t>& rgbaOut, std::string& err) {
    if (!ValidateRequest(request, err)) return false;

    VkContext ctx;
    std::string vkErr;
    if (!ctx.Init(/*presentSupport=*/false, vkErr)) {
        // Plain string convention, deliberately -- see this function's own
        // doc comment (RenderToImage.h): lets a caller with zero Vulkan
        // knowledge distinguish "no device, treat as SKIP" from a real
        // render failure without including a single Vulkan header.
        err = "no Vulkan device: " + vkErr;
        return false;
    }

    bool ok = RenderToImage(ctx, request, rgbaOut, err);
    ctx.Shutdown();
    return ok;
}

} // namespace Onyx::Rendering
