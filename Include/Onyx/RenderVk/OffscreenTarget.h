#pragma once

// See VkContext.h for the binding include-order rule (volk.h, then
// vk_mem_alloc.h, before any other Vulkan-touching header). VkResources.h
// already pulls both in (via VkContext.h) in that order, so including it
// first here keeps the rule honored without repeating it.
#include <Onyx/RenderVk/VkResources.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Onyx::RenderVk {

/// Offscreen render target: RGBA8 color + D32 depth, both 4x MSAA
/// (kSampleCount, Pipelines.h) with a single-sample RGBA8 resolve image
/// readback goes through. Mirrors Tools/OnyxOracle/HeadlessGL's shape
/// (Create/BeginFrame/EndFrame/Readback) so T7's parity harness can drive
/// either renderer through the same call sequence.
///
/// Dynamic rendering (no VkRenderPass/VkFramebuffer) targets kColorFormat/
/// kDepthFormat via VkRenderingInfo -- every pipeline T3 built already
/// pins rasterizationSamples=kSampleCount and those two formats
/// (Pipelines.h), so this is the only attachment shape a T5 draw can use
/// against this target.
///
/// Lifecycle: Create() once, then any number of
/// BeginFrame()/[draws]/EndFrame()/Readback() cycles, then Destroy(). Not
/// copyable (owns live GPU images) -- a plain caller-owned value like
/// VkResources' Buffer/Image2D, just non-trivial enough to need the copy
/// ops explicitly deleted rather than left implicit.
///
/// BeginFrame/EndFrame bracket a caller-owned command buffer (typically a
/// Resources::OneShot scope). Readback is a *separate* GPU round trip (its
/// own internal OneShot) and does not take a VkCommandBuffer -- it must be
/// called only after the command buffer holding the matching EndFrame has
/// itself been submitted and waited on (Resources::OneShot already blocks
/// until GPU completion, so calling Readback() right after the OneShot()
/// that ran BeginFrame/EndFrame returns is exactly correct).
class OffscreenTarget {
public:
    OffscreenTarget() = default;

    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;

    /// Allocates the MSAA color/depth images and the single-sample resolve
    /// color image at w x h. Returns false and fills err on failure
    /// (VMA/image-view creation), leaving nothing to Destroy(). w and h
    /// must be positive. Fails (with err set, no state changed) if called
    /// on an already-created target -- call Destroy() first to resize.
    bool Create(VkContext& ctx, int w, int h, std::string& err);

    /// Idempotent; safe to call on a target that was never Create()'d.
    void Destroy(VkContext& ctx);

    /// Transitions the MSAA color/depth and resolve images to their
    /// dynamic-rendering attachment layouts, then begins rendering on the
    /// MSAA target: color cleared to `clear` (RGBA, 0..1 each), depth
    /// cleared to 1.0, viewport/scissor set to the full target. The
    /// viewport is the standard (non-negated-height) Vulkan viewport --
    /// Pipelines.h's Camera convention handles the Vulkan/GL NDC-Y
    /// difference in the projection matrix a draw's shader consumes, not
    /// here; this target never negates its viewport.
    void BeginFrame(VkCommandBuffer cmd, const float clear[4]);

    /// Ends dynamic rendering -- the MSAA color attachment resolves into
    /// the single-sample resolve image as part of this call
    /// (VK_RESOLVE_MODE_AVERAGE_BIT, wired up by BeginFrame), then
    /// transitions the resolve image to TRANSFER_SRC_OPTIMAL so
    /// Readback() can copy it.
    void EndFrame(VkCommandBuffer cmd);

    /// Copies the resolve image to a host-visible staging buffer (its own
    /// Resources::OneShot scope, waited to completion) and returns it as
    /// tightly packed RGBA8, row 0 first -- empirically verified top-down
    /// for this target (task-4 smoke: a background split into a distinct
    /// TOP/BOTTOM color, readback row 0 checked against TOP), matching
    /// Vulkan's own image-storage row order under the standard
    /// (non-negated) viewport BeginFrame sets up. No flip is performed
    /// here, unlike Tools/OnyxOracle/HeadlessGL::EndFrame, which must flip
    /// because GL's glReadPixels hands back bottom-up.
    ///
    /// Returns false and fills err if the target was never Create()'d, or
    /// if called without a completed EndFrame first (the resolve image is
    /// not sitting in TRANSFER_SRC_OPTIMAL).
    ///
    /// Fence discipline (T4-review rider): the check above is a CPU-side
    /// layout flag on THIS object, not a GPU-side fact -- it only catches
    /// "EndFrame was never called on this target." It cannot detect a
    /// frame that was recorded but never submitted, or submitted but not
    /// yet waited on: m_resolveLayout flips to TRANSFER_SRC_OPTIMAL the
    /// instant EndFrame() finishes RECORDING the transition, regardless of
    /// whether the command buffer holding it has actually reached the GPU
    /// yet. Every caller in this codebase today (--vk-smoke, T5's
    /// SceneRendererVk smoke path) goes through Resources::OneShot for the
    /// BeginFrame/EndFrame command buffer, which blocks on a fence before
    /// returning -- so by the time Readback() runs, completion is already
    /// guaranteed and this is safe. A caller OUTSIDE that OneShot-per-frame
    /// pattern (e.g. a persistent command buffer submitted once and reused
    /// across frames) MUST fence-wait that frame's submission itself
    /// before calling Readback(), or the copy races the render. The real
    /// per-frame submit/fence-wait/present discipline lands with the Shell
    /// swapchain integration (T9); until then, OneShot's blocking wait is
    /// what keeps every existing caller correct.
    bool Readback(VkContext& ctx, std::vector<uint8_t>& rgbaTopDown, std::string& err);

    int Width()  const { return m_width; }
    int Height() const { return m_height; }

private:
    Image2D m_msaaColor;
    Image2D m_msaaDepth;
    Image2D m_resolveColor;
    int     m_width  = 0;
    int     m_height = 0;

    // Layouts this object left each image in -- tracked so BeginFrame only
    // ever issues a correct oldLayout->newLayout transition (never assumes
    // UNDEFINED past the very first frame).
    VkImageLayout m_colorLayout   = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout m_depthLayout   = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout m_resolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

} // namespace Onyx::RenderVk
