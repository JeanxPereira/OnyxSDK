#pragma once
#include <Onyx/Viewers/IDocumentContent.h>
#include <Onyx/Parsers/TextureData.h>
#include <imgui.h>
#include <string>
#include <memory>
#include <vector>
#include <cstdint>

namespace Onyx::App { class TexturePool; }

namespace Onyx::Viewers {

class ImageViewer : public IDocumentContent {
public:
    ImageViewer(const std::string& name, std::unique_ptr<Parsers::TextureData> texture);
    ~ImageViewer() override;

    std::string GetName() const override;
    void Draw() override;

private:
    std::string m_name;
    std::unique_ptr<Parsers::TextureData> m_texture;

    // T10: Vulkan texture via Onyx::App::TexturePool (replaces GL m_glTexture).
    // Owns its own pool instance (see TexturePool.h's ownership model).
    std::unique_ptr<Onyx::App::TexturePool> m_texPool;
    ImTextureID m_texId = 0; // ImTextureID_Invalid until UploadToGPU() succeeds
    bool m_showAlpha = false;

    // Pan + zoom state. *Target values are the immediate result of input;
    // unsuffixed values lerp toward them each frame for smooth motion.
    float  m_zoom       = 1.0f;
    float  m_zoomTarget = 1.0f;
    ImVec2 m_pan{0, 0};
    ImVec2 m_panTarget{0, 0};
    bool   m_initialFitDone = false;

    void UploadToGPU();
    // Re-derives the displayed RGBA buffer (normal, or alpha-as-grayscale
    // when m_showAlpha is set) and pushes it into the SAME pooled texture
    // via TexturePool::Update -- the GL path instead toggled a live
    // VkImageView-less GL_TEXTURE_SWIZZLE_RGBA parameter on the same
    // texture object; Vulkan's ImGui backend descriptor has no per-draw
    // swizzle knob, so this recomputes pixels on the CPU instead (toggled
    // rarely, on a user click -- not a per-frame cost).
    void ApplyAlphaToggle();
    // Unified zoom helper: drive zoom toward newZoom while keeping the screen
    // point anchorScreen invariant (panTarget adjusts so the same image-local
    // point stays under that screen position once the lerp finishes).
    void ZoomToAnchored(float newZoom, ImVec2 anchorScreen);
};

} // namespace Onyx::Viewers
