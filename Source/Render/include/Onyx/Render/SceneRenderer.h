#pragma once

#include <Onyx/Core/Domain/Scene.h>
#include <Onyx/Core/Result.h>
#include <Onyx/Core/Services/Diagnostics.h>
#include <Onyx/Gpu/Descriptors.h>
#include <Onyx/Gpu/Device.h>
#include <Onyx/Gpu/FrameRing.h>
#include <Onyx/Gpu/RenderTarget.h>
#include <Onyx/Gpu/Upload.h>
#include <Onyx/Render/GpuScene.h>
#include <Onyx/Render/View.h>

#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

namespace Onyx::Render {

struct ScenePipelines;
struct OverlayPlan;

struct RendererOptions {
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_4_BIT;
    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
};

// Loads scenes onto the GPU and records their draws. One renderer serves any number of scenes and
// render targets whose formats and sample count match its options. It keeps descriptor sets keyed
// by the id of the FrameRing and the transient buffer of a frame slot. When a Draw sees a different
// ring than the last one, it waits for the graphics queue to finish what was submitted, resets its
// descriptor pools and clears the caches, so a replaced ring never meets a stale set. Each pool holds
// 64 sets; the frame pool needs one set per frame slot and distinct palette range, so exhausting it
// reports gpu.vk with a message naming kSets in SceneRenderer.cpp. Must be destroyed before its
// Device. Not thread-safe.
//
// Error codes: render.target.format (the target does not match RendererOptions), render.scene.device
// (the scene belongs to another Device), render.part.vertices (the scene's vertices or indices exceed
// the buffer addressing range), plus the gpu.* codes of the layer below.
// Diagnostic codes:
//   render.skinning.*       a skipped part (jointMap, jointIndex, streams) or a bad joint order
//   render.texture.size     a texture the uploader rejected; the default is bound
//   render.material.index   a material or texture index outside its pool; the default is used
//   render.part.index       an index outside the part's vertices; part skipped
//   render.part.vertices    see above
//   render.palette.size     SetPalette was given the wrong size and ignored
//   render.worlds.size      SetWorldTransforms was given the wrong size and ignored
//   render.pose.ignored     View::pose was set; only RenderToImage evaluates it
class SceneRenderer {
public:
    // Builds every pipeline once through PipelineBuilder and the device pipeline cache.
    static Result<std::unique_ptr<SceneRenderer>> Create(Gpu::Device& device, const RendererOptions& options = {});

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;
    ~SceneRenderer();

    // Records every upload and submits them with one UploadContext::Flush; the scene is ready at
    // GpuScene::Ready(). Offending parts are reported to `diags` and skipped.
    Result<GpuScene> Load(const Domain::Scene& scene, Gpu::UploadContext& upload, Services::DiagSink* diags = nullptr);

    // Records the target pass into frame.Cmd() and makes the frame wait for the scene's upload. It
    // submits nothing: the host ends the frame.
    Result<void> Draw(Gpu::Frame& frame, Gpu::RenderTarget& target, const GpuScene& scene, const View& view,
                      const Overlays& overlays, Services::DiagSink* diags = nullptr);

    const RendererOptions& Options() const { return m_options; }
    Gpu::Device& GetDevice() const { return m_device; }

private:
    using DescriptorCache = std::map<std::pair<uint64_t, VkBuffer>, VkDescriptorSet>;

    SceneRenderer(Gpu::Device& device, const RendererOptions& options);

    Result<void> Populate(GpuScene& out, const Domain::Scene& scene, Gpu::UploadContext& upload,
                          Services::DiagSink* diags);
    Result<VkDescriptorSet> FrameSet(uint64_t ringId, VkBuffer transient, VkDeviceSize paletteRange);
    Result<VkDescriptorSet> BackgroundSet(uint64_t ringId, VkBuffer transient);
    Result<VkDescriptorSet> UniformSet(std::optional<Gpu::DescriptorPool>& pool, DescriptorCache& sets,
                                       VkDescriptorSetLayout layout, uint64_t ringId, VkBuffer transient,
                                       VkDeviceSize range);
    Result<void> BindRing(uint64_t ringId);
    Result<OverlayPlan> PlanOverlays(Gpu::Frame& frame, const GpuScene& scene, const View& view,
                                     const Overlays& overlays);
    void RecordOverlays(VkCommandBuffer cmd, const OverlayPlan& plan);

    Gpu::Device& m_device;
    RendererOptions m_options;
    std::unique_ptr<ScenePipelines> m_pipelines;
    std::optional<Gpu::DescriptorPool> m_framePool;
    std::optional<Gpu::DescriptorPool> m_backgroundPool;
    uint64_t m_ringId = 0;
    std::map<std::tuple<uint64_t, VkBuffer, VkDeviceSize>, VkDescriptorSet> m_frameSets;
    DescriptorCache m_backgroundSets;
    std::optional<Gpu::DescriptorPool> m_gridPool;
    std::optional<Gpu::DescriptorPool> m_overlayPool;
    DescriptorCache m_gridSets;
    DescriptorCache m_overlaySets;
};

}
