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
#include <utility>
#include <unordered_map>

namespace Onyx::Render {

struct ScenePipelines;

struct RendererOptions {
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_4_BIT;
    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
};

// Loads scenes onto the GPU and records their draws. One renderer serves any number of scenes and
// render targets whose formats and sample count match its options. It keeps descriptor sets keyed
// by the transient buffer of a frame slot, so it must be paired with one FrameRing at a time and
// recreated when that ring is replaced. Must be destroyed before its Device. Not thread-safe.
//
// Error codes: render.target.format (the target does not match RendererOptions), render.scene.device
// (the scene belongs to another Device), plus the gpu.* codes of the layer below.
// Diagnostics: render.skinning.* (a skipped part), render.part.index (an index outside the vertex
// range, part skipped), render.material.index (a material or texture index outside its pool),
// render.texture.size (a texture the uploader rejected), render.palette.size (SetPalette ignored).
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

private:
    SceneRenderer(Gpu::Device& device, const RendererOptions& options);

    Result<void> Populate(GpuScene& out, const Domain::Scene& scene, Gpu::UploadContext& upload,
                          Services::DiagSink* diags);
    Result<VkDescriptorSet> FrameSet(VkBuffer transient, VkDeviceSize paletteRange);
    Result<VkDescriptorSet> BackgroundSet(VkBuffer transient);
    void DrawOverlays(VkCommandBuffer cmd, const GpuScene& scene, const View& view, const Overlays& overlays);

    Gpu::Device& m_device;
    RendererOptions m_options;
    std::unique_ptr<ScenePipelines> m_pipelines;
    std::optional<Gpu::DescriptorPool> m_framePool;
    std::optional<Gpu::DescriptorPool> m_backgroundPool;
    std::map<std::pair<VkBuffer, VkDeviceSize>, VkDescriptorSet> m_frameSets;
    std::unordered_map<VkBuffer, VkDescriptorSet> m_backgroundSets;
};

}
