#pragma once

#include <Onyx/Core/Domain/Texture.h>
#include <Onyx/Core/Result.h>
#include <Onyx/Ui/Panel.h>

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>

namespace Onyx::Ui {

class TexturePool;

// Zoom, pan, fit and an alpha toggle over one texture. The texture is uploaded on the first draw that
// has a texture pool; the view removes it again on destruction, so the pool must outlive the view.
class ImageView : public IDocumentView {
public:
    ImageView(Modules::DocumentId doc, Modules::NodePath node, std::string name, Domain::Texture texture);
    ~ImageView() override;

    ImageView(const ImageView&) = delete;
    ImageView& operator=(const ImageView&) = delete;

    std::string_view Id() const override { return m_id; }
    std::string_view Title() const override { return m_name; }
    void Draw(UiContext& ui) override;
    Modules::DocumentId Document() const override { return m_doc; }
    Modules::NodePath Node() const override { return m_node; }

    const Domain::Texture& Texture() const { return m_texture; }
    // The upload failure, if any, as the pool reported it.
    const std::optional<Error>& Fault() const { return m_fault; }
    bool Uploaded() const { return m_color != ImTextureID_Invalid; }
    // Shows the alpha channel as grey. Only RGBA8 textures have one to show.
    void ShowAlpha(bool on) { m_showAlpha = on; }
    bool AlphaShown() const { return m_showAlpha; }

private:
    void Upload(TexturePool& pool);
    void UploadAlpha(TexturePool& pool);
    void ZoomTo(float zoom, ImVec2 anchor);

    Modules::DocumentId m_doc;
    Modules::NodePath m_node;
    std::string m_name;
    std::string m_id;
    Domain::Texture m_texture;
    TexturePool* m_pool = nullptr;
    ImTextureID m_color = ImTextureID_Invalid;
    ImTextureID m_alpha = ImTextureID_Invalid;
    std::optional<Error> m_fault;
    bool m_showAlpha = false;
    bool m_fitted = false;
    float m_zoom = 1.0f;
    float m_zoomTarget = 1.0f;
    ImVec2 m_pan{0.0f, 0.0f};
    ImVec2 m_panTarget{0.0f, 0.0f};
};

}
