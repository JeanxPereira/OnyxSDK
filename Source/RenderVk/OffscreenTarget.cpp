#include <Onyx/RenderVk/OffscreenTarget.h>
#include <Onyx/RenderVk/Pipelines.h> // kColorFormat/kDepthFormat/kSampleCount

#include <cstring>

namespace Onyx::Rendering {

namespace {

// Batched image-layout transition helper -- every OffscreenTarget barrier
// site (BeginFrame's three attachments, EndFrame's resolve-to-transfer-src)
// follows the same shape as VkResources.cpp's UploadImage barriers
// (vkCmdPipelineBarrier2 / synchronization2), just parameterised here since
// BeginFrame needs three of them batched into one VkDependencyInfo call.
void FillBarrier(VkImageMemoryBarrier2& b, VkImage img, VkImageAspectFlags aspect,
                  VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                  VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    b = VkImageMemoryBarrier2{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {aspect, 0, 1, 0, 1};
}

} // namespace

bool OffscreenTarget::Create(VkContext& ctx, int w, int h, std::string& err) {
    if (m_msaaColor.img != VK_NULL_HANDLE) {
        err = "OffscreenTarget::Create called on an already-created target";
        return false;
    }
    if (w <= 0 || h <= 0) {
        err = "OffscreenTarget::Create: width/height must be positive";
        return false;
    }

    m_msaaColor = Resources::CreateImage2D(ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                            kColorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                            kSampleCount, err);
    if (m_msaaColor.img == VK_NULL_HANDLE) {
        err = "OffscreenTarget::Create: msaa color image: " + err;
        return false;
    }

    m_msaaDepth = Resources::CreateImage2D(ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                            kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                            kSampleCount, err);
    if (m_msaaDepth.img == VK_NULL_HANDLE) {
        err = "OffscreenTarget::Create: msaa depth image: " + err;
        Resources::Destroy(ctx, m_msaaColor);
        return false;
    }

    // SAMPLED_BIT (T10 addition): every existing caller (Readback(), the
    // oracle's parity harness) only ever reads this image back to the CPU
    // via TRANSFER_SRC, so adding SAMPLED here changes nothing for them --
    // it exists so a caller can also bind this image as a descriptor (T10's
    // Viewport3D, via PrepareForSampling() + ImGui_ImplVulkan_AddTexture())
    // without a second, parallel-owned image.
    m_resolveColor = Resources::CreateImage2D(
        ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h), kColorFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_SAMPLE_COUNT_1_BIT, err);
    if (m_resolveColor.img == VK_NULL_HANDLE) {
        err = "OffscreenTarget::Create: resolve color image: " + err;
        Resources::Destroy(ctx, m_msaaDepth);
        Resources::Destroy(ctx, m_msaaColor);
        return false;
    }

    m_width = w;
    m_height = h;
    m_colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_resolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void OffscreenTarget::Destroy(VkContext& ctx) {
    Resources::Destroy(ctx, m_resolveColor);
    Resources::Destroy(ctx, m_msaaDepth);
    Resources::Destroy(ctx, m_msaaColor);
    m_width = 0;
    m_height = 0;
    m_colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_resolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void OffscreenTarget::BeginFrame(VkCommandBuffer cmd, const float clear[4]) {
    // Dynamic rendering does not transition attachment images itself --
    // the caller must land each one in its attachment-optimal layout
    // before vkCmdBeginRendering, same as VkResources::UploadImage does
    // for its own transfer-dst/shader-read transitions. oldLayout comes
    // from what this object itself left the image in (UNDEFINED only on
    // the very first frame after Create(); COLOR/DEPTH_ATTACHMENT_OPTIMAL
    // on every frame after the first; TRANSFER_SRC_OPTIMAL for the resolve
    // image on every frame after the first Readback(); SHADER_READ_ONLY_
    // OPTIMAL for the resolve image on every frame after the first
    // PrepareForSampling() -- T10).
    VkImageMemoryBarrier2 barriers[3];
    FillBarrier(barriers[0], m_msaaColor.img, VK_IMAGE_ASPECT_COLOR_BIT, m_colorLayout,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    FillBarrier(barriers[1], m_msaaDepth.img, VK_IMAGE_ASPECT_DEPTH_BIT, m_depthLayout,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    // T10 fix-round-1 (HIGH, reviewer-traced write-after-read): when the
    // resolve image is coming FROM SHADER_READ_ONLY_OPTIMAL (a prior
    // PrepareForSampling(), i.e. some OTHER queue submission -- the
    // swapchain frame's own ImGui draw, sampling this exact image via
    // imgui_impl_vulkan -- may still be reading it, with no semaphore
    // tying that submission to this one), the barrier's src side must name
    // that read explicitly: srcStage=FRAGMENT_SHADER + srcAccess=
    // SHADER_SAMPLED_READ. This is a queue-scoped acquire barrier: naming
    // every stage/access a prior submission on the SAME queue could still
    // be doing synchronizes against it without needing a semaphore, which
    // is the minimal correct fix (sync2's execution/memory dependency
    // already covers same-queue ordering; a cross-queue wait is not
    // needed here). TOP_OF_PIPE/NONE stays correct for the other two
    // reachable prior layouts: UNDEFINED (first use after Create(),
    // nothing to wait on) and TRANSFER_SRC_OPTIMAL (Readback()'s own
    // path, whose caller already fences via Resources::OneShot before
    // this could ever run again).
    VkPipelineStageFlags2 resolveSrcStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    VkAccessFlags2        resolveSrcAccess = VK_ACCESS_2_NONE;
    if (m_resolveLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        resolveSrcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        resolveSrcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    }
    FillBarrier(barriers[2], m_resolveColor.img, VK_IMAGE_ASPECT_COLOR_BIT, m_resolveLayout,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, resolveSrcStage, resolveSrcAccess,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 3;
    dep.pImageMemoryBarriers = barriers;
    vkCmdPipelineBarrier2(cmd, &dep);

    m_colorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    m_depthLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    m_resolveLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = m_msaaColor.view;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // Color is a non-integer (UNORM) format, so AVERAGE resolve is valid
    // per the Vulkan spec (average is only disallowed for integer color
    // formats) -- matches the GL_LINEAR blit HeadlessGL uses for its own
    // MSAA resolve (glBlitFramebuffer(..., GL_NEAREST) there is a
    // different tradeoff HeadlessGL made for its own reasons; this target
    // follows the MSAA-standard averaging resolve instead).
    colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    colorAttachment.resolveImageView = m_resolveColor.view;
    colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // DONT_CARE: only the resolve target's contents matter after
    // EndFrame -- the MSAA image itself is transient scratch every frame.
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.clearValue.color.float32[0] = clear[0];
    colorAttachment.clearValue.color.float32[1] = clear[1];
    colorAttachment.clearValue.color.float32[2] = clear[2];
    colorAttachment.clearValue.color.float32[3] = clear[3];

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = m_msaaDepth.view;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // never read back
    depthAttachment.clearValue.depthStencil.depth = 1.0f;
    depthAttachment.clearValue.depthStencil.stencil = 0;

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea.offset = {0, 0};
    renderInfo.renderArea.extent = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)};
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;
    renderInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderInfo);

    // Standard (non-negated) viewport -- see this method's header comment
    // and Pipelines.h's Camera convention: the Vulkan/GL NDC-Y difference
    // is handled by negating proj[1][1] in whatever projection matrix a
    // draw's shader consumes, never by flipping the viewport here.
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_width);
    viewport.height = static_cast<float>(m_height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void OffscreenTarget::EndFrame(VkCommandBuffer cmd) {
    // The MSAA color attachment resolves into m_resolveColor as part of
    // this call -- BeginFrame already wired resolveImageView/
    // resolveImageLayout onto the color attachment info above.
    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 toSrc;
    FillBarrier(toSrc, m_resolveColor.img, VK_IMAGE_ASPECT_COLOR_BIT, m_resolveLayout,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &toSrc;
    vkCmdPipelineBarrier2(cmd, &dep);

    m_resolveLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
}

void OffscreenTarget::PrepareForSampling(VkCommandBuffer cmd) {
    if (m_resolveLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        return; // already there -- no-op barrier avoided entirely, not just skipped

    VkImageMemoryBarrier2 toShaderRead;
    FillBarrier(toShaderRead, m_resolveColor.img, VK_IMAGE_ASPECT_COLOR_BIT, m_resolveLayout,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT);

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &toShaderRead;
    vkCmdPipelineBarrier2(cmd, &dep);

    m_resolveLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

bool OffscreenTarget::Readback(VkContext& ctx, std::vector<uint8_t>& rgbaTopDown, std::string& err) {
    if (m_resolveColor.img == VK_NULL_HANDLE) {
        err = "OffscreenTarget::Readback: target was not Create()'d";
        return false;
    }
    // T4-review rider: this is a CPU-side bookkeeping check (has EndFrame()
    // ever recorded the resolve-image transition on this object?), not a
    // GPU-side completion check -- it cannot tell a submitted-and-finished
    // frame apart from one that is merely recorded, or even submitted but
    // still in flight. See the fence-discipline paragraph on the header
    // declaration (OffscreenTarget.h) for what callers outside the
    // OneShot-per-frame pattern this codebase uses today must do instead.
    if (m_resolveLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        err = "OffscreenTarget::Readback: called without a completed EndFrame first "
              "(resolve image is not in TRANSFER_SRC_OPTIMAL)";
        return false;
    }

    const VkDeviceSize size =
        static_cast<VkDeviceSize>(m_width) * static_cast<VkDeviceSize>(m_height) * 4;

    Buffer staging = Resources::CreateBuffer(ctx, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                              VMA_MEMORY_USAGE_GPU_TO_CPU, err);
    if (staging.buf == VK_NULL_HANDLE) {
        err = "OffscreenTarget::Readback: staging buffer: " + err;
        return false;
    }

    const int width = m_width;
    const int height = m_height;
    VkImage srcImage = m_resolveColor.img;

    bool ok = Resources::OneShot(ctx, [&](VkCommandBuffer cmd) {
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;   // tightly packed
        region.bufferImageHeight = 0; // tightly packed
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        vkCmdCopyImageToBuffer(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buf, 1,
                                &region);
    }, err);
    if (!ok) {
        err = "OffscreenTarget::Readback: " + err;
        Resources::Destroy(ctx, staging);
        return false;
    }

    VmaAllocationInfo stagingInfo{};
    vmaGetAllocationInfo(ctx.Allocator(), staging.alloc, &stagingInfo);
    if (!stagingInfo.pMappedData) {
        err = "OffscreenTarget::Readback: staging buffer is not host-mapped";
        Resources::Destroy(ctx, staging);
        return false;
    }

    // No bottom-up-to-top-down flip here, unlike HeadlessGL::EndFrame --
    // see Readback()'s header comment: verified empirically (task-4
    // smoke) that this copy already comes back top-down.
    rgbaTopDown.resize(static_cast<size_t>(size));
    std::memcpy(rgbaTopDown.data(), stagingInfo.pMappedData, static_cast<size_t>(size));

    Resources::Destroy(ctx, staging);
    return true;
}

} // namespace Onyx::Rendering
